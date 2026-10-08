#include "axiom/ops/ops_services.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/topology_materialization.h"
#include "axiom/internal/math/math_internal_utils.h"
#include "axiom/internal/ops/ops_service_internal.h"

namespace axiom {
namespace {

using ops_internal::BooleanPlanarFace;

struct BooleanReadFailure {
    StatusCode status {StatusCode::Ok};
    std::string_view code {};
    const char* message {""};
};

Scalar point_resolution(const Point3& point) {
    return 64 * std::numeric_limits<Scalar>::epsilon() *
        std::max({Scalar(1), std::abs(point.x), std::abs(point.y), std::abs(point.z)});
}

bool finite_point(const Point3& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

bool same_point(const Point3& a, const Point3& b) {
    return detail::norm(detail::subtract(a,b)) <= std::max(point_resolution(a),point_resolution(b));
}

bool consume_work(std::size_t& work, std::size_t limit, std::size_t count = 1) {
    if (count > limit - work) return false;
    work += count;
    return true;
}

// Read-only closed-shell validation is also needed for point queries, which
// have no second body and therefore cannot use the pairwise intersection gate.
bool read_classification_body(const detail::KernelState& state, BodyId id,
                               const BooleanIntersectionOptions& options,
                               std::vector<BooleanPlanarFace>& faces, BooleanReadFailure& failure, std::size_t& work) {
    const auto reject = [&](StatusCode status, std::string_view code, const char* message) {
        failure = {status,code,message};
        return false;
    };
    const auto& t = options.tolerance;
    if (!detail::valid_tolerance_policy(t) || !std::isfinite(t.linear) || !std::isfinite(t.angular) ||
        !std::isfinite(t.min_local) || !std::isfinite(t.max_local) || !(t.linear > 0) ||
        !(t.angular > 0) || t.angular >= 1 || t.linear < t.min_local || t.linear > t.max_local ||
        options.max_face_pairs == 0 || options.max_face_pairs > 2000000 ||
        options.max_segments == 0 || options.max_segments > 100000 ||
        options.max_edges_per_face < 3 || options.max_edges_per_face > 256 ||
        !detail::has_body(state,id))
        return reject(StatusCode::InvalidInput,diag_codes::kBoolInvalidInput,"分类输入、容差或预算无效");
    if (t.precision_mode != PrecisionMode::FastFloat && t.precision_mode != PrecisionMode::AdaptiveCertified &&
        t.precision_mode != PrecisionMode::ExactCritical)
        return reject(StatusCode::InvalidInput,diag_codes::kBoolInvalidInput,"分类精度模式无效");
    if (t.precision_mode == PrecisionMode::ExactCritical)
        return reject(StatusCode::NotImplemented,diag_codes::kBoolUnsupportedInput,"分类不支持请求的精度模式");
    const auto& body = state.bodies.at(id.value);
    if (body.rep_kind != RepKind::ExactBRep || body.shells.empty())
        return reject(StatusCode::NotImplemented,diag_codes::kBoolUnsupportedInput,"分类要求真实平面直边闭壳");
    std::unordered_set<std::uint64_t> seen;
    for (const auto shell_id : body.shells) {
        const auto shell = state.shells.find(shell_id.value);
        if (shell == state.shells.end() || shell->second.faces.empty())
            return reject(StatusCode::InvalidTopology,diag_codes::kBoolInvalidInput,"分类输入壳缺失或为空");
        std::unordered_map<std::uint64_t,std::array<int,2>> uses;
        for (const auto fid : shell->second.faces) {
            if (!seen.insert(fid.value).second)
                return reject(StatusCode::InvalidTopology,diag_codes::kBoolInvalidInput,"分类输入重复引用面");
            if (!consume_work(work,options.max_face_pairs))
                return reject(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"分类读取超过面预算");
            BooleanPlanarFace face;
            StatusCode status {};
            if (!ops_internal::read_boolean_planar_face(state,fid,t.linear,options.max_edges_per_face,face,status))
                return reject(status,status == StatusCode::NotImplemented ? diag_codes::kBoolUnsupportedInput :
                    status == StatusCode::OperationFailed ? diag_codes::kBoolPreparationBudgetExceeded :
                    status == StatusCode::NumericalInstability ? diag_codes::kBoolNumericalFailure :
                    diag_codes::kBoolInvalidInput,"分类面边界读取失败");
            for (const auto& ring : face.edges)
                if (!consume_work(work,options.max_face_pairs,ring.size()))
                    return reject(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"分类读取超过边预算");
            const auto& record = state.faces.at(fid.value);
            std::vector<LoopId> loops {record.outer_loop};
            loops.insert(loops.end(),record.inner_loops.begin(),record.inner_loops.end());
            for (const auto loop : loops) for (const auto cid : state.loops.at(loop.value).coedges) {
                const auto& coedge = state.coedges.at(cid.value);
                auto& use = uses[coedge.edge_id.value];
                ++use[0];
                use[1] += coedge.reversed ? -1 : 1;
            }
            faces.push_back(std::move(face));
        }
        if (std::any_of(uses.begin(),uses.end(),[](const auto& use) {
            return use.second[0] != 2 || use.second[1] != 0;
        })) return reject(StatusCode::InvalidTopology,diag_codes::kBoolInvalidInput,"分类要求闭合且一致定向的壳边界");
    }
    return true;
}

constexpr std::array<Vec3,6> kBooleanRayDirections {{ {1,0.371,0.529}, {-0.217,1,0.613},
    {0.419,-0.283,1}, {1,0.733,-0.337}, {-0.593,1,-0.431}, {0.677,0.239,1} }};

// Read-only classification candidates. Bounds never determine material; the
// original support-plane, trim and independent-ray predicates do that below.
struct BooleanClassificationIndex {
    struct Node { BoundingBox bounds{}; std::size_t begin{}, end{}; int left{-1}, right{-1}; };
    std::vector<Node> nodes;
    std::vector<std::size_t> order;
    std::vector<std::array<Scalar,6>> denominators;
    std::array<Vec3,6> directions{};
    std::array<bool,6> invalid_directions{};
    std::array<std::vector<std::size_t>,6> parallel_faces;
    std::array<Scalar,6> min_denominators{};
    Scalar origin_scale{1}, normal_scale{1};
};

bool build_classification_index(const std::vector<BooleanPlanarFace>& faces,
                                BooleanClassificationIndex& index,
                                std::size_t& work, std::size_t limit) {
    index.min_denominators.fill(std::numeric_limits<Scalar>::infinity());
    for (std::size_t ray = 0; ray < 6; ++ray)
        index.directions[ray] = detail::normalize(kBooleanRayDirections[ray]);
    for (std::size_t face = 0; face < faces.size(); ++face) {
        if (!consume_work(work,limit)) return false;
        index.order.push_back(face);
        const auto& source = faces[face];
        index.origin_scale = std::max({index.origin_scale,std::abs(source.origin.x),
            std::abs(source.origin.y),std::abs(source.origin.z)});
        index.normal_scale = std::max(index.normal_scale,
            std::abs(source.normal.x)+std::abs(source.normal.y)+std::abs(source.normal.z));
        std::array<Scalar,6> denominators{};
        for (std::size_t ray = 0; ray < 6; ++ray) {
            if (!consume_work(work,limit)) return false;
            denominators[ray] = detail::dot(source.normal,index.directions[ray]);
            const auto magnitude = std::abs(denominators[ray]);
            if (magnitude <= 128 * std::numeric_limits<Scalar>::epsilon())
                index.parallel_faces[ray].push_back(face);
            else if (!std::isfinite(magnitude) || magnitude <= std::sqrt(std::numeric_limits<Scalar>::epsilon()))
                index.invalid_directions[ray] = true;
            else index.min_denominators[ray] = std::min(index.min_denominators[ray],magnitude);
        }
        index.denominators.push_back(denominators);
    }
    const auto coordinate = [](const Point3& point, std::size_t axis) {
        return axis == 0 ? point.x : axis == 1 ? point.y : point.z;
    };
    const auto build = [&](auto&& self, std::size_t begin, std::size_t end) -> int {
        BooleanClassificationIndex::Node node;
        node.begin = begin; node.end = end;
        for (std::size_t i = begin; i < end; ++i) {
            if (!consume_work(work,limit,2)) return -1;
            const auto& box = faces[index.order[i]].bbox;
            detail::extend_materialization_bbox(node.bounds,box.min);
            detail::extend_materialization_bbox(node.bounds,box.max);
        }
        const int id = static_cast<int>(index.nodes.size());
        index.nodes.push_back(node);
        if (end-begin > 8) {
            std::size_t axis = 0;
            for (std::size_t candidate = 1; candidate < 3; ++candidate)
                if (static_cast<long double>(coordinate(node.bounds.max,candidate))-coordinate(node.bounds.min,candidate) >
                    static_cast<long double>(coordinate(node.bounds.max,axis))-coordinate(node.bounds.min,axis)) axis = candidate;
            const std::size_t middle = begin+(end-begin)/2;
            std::nth_element(index.order.begin()+static_cast<std::ptrdiff_t>(begin),
                index.order.begin()+static_cast<std::ptrdiff_t>(middle),
                index.order.begin()+static_cast<std::ptrdiff_t>(end),[&](std::size_t a,std::size_t b) {
                    const auto& first = faces[a].bbox;
                    const auto& second = faces[b].bbox;
                    return static_cast<long double>(coordinate(first.min,axis))+coordinate(first.max,axis) <
                        static_cast<long double>(coordinate(second.min,axis))+coordinate(second.max,axis);
                });
            node.left = self(self,begin,middle);
            if (node.left < 0) return -1;
            node.right = self(self,middle,end);
            if (node.right < 0) return -1;
            index.nodes[static_cast<std::size_t>(id)] = node;
        }
        return id;
    };
    return faces.empty() || build(build,0,faces.size()) >= 0;
}

// Rays that touch an edge/vertex or graze a support plane are discarded. Two
// independently resolved directions must agree; disagreement never falls back
// to bounding boxes. Boundary uncertainty is rejected before counting crossings.
bool classify_boolean_point(const detail::KernelState& state,
                             const std::vector<BooleanPlanarFace>& faces,
                             const BooleanClassificationIndex* index, const Point3& point,
                             Scalar tolerance, std::size_t& work, std::size_t limit,
                             BooleanPointClassification& out, BooleanReadFailure& failure) {
    const auto reject = [&](StatusCode status, std::string_view code, const char* message) {
        failure = {status,code,message};
        return false;
    };
    if (!finite_point(point))
        return reject(StatusCode::InvalidInput,diag_codes::kBoolInvalidInput,"分类点坐标必须有限");
    if (point_resolution(point) > tolerance)
        return reject(StatusCode::NumericalInstability,diag_codes::kBoolNumericalFailure,"分类点坐标无法在容差内分辨");
    const long double point_scale = std::max({Scalar(1),std::abs(point.x),std::abs(point.y),std::abs(point.z)});
    const long double origin_scale = index ? index->origin_scale : 0;
    const long double normal_scale = index ? index->normal_scale : 1;
    const long double margin = 4*static_cast<long double>(tolerance);
    const auto coordinate = [](const Point3& position, std::size_t axis) {
        return axis == 0 ? position.x : axis == 1 ? position.y : position.z;
    };
    const auto candidates = [&](const Vec3* direction, std::vector<std::size_t>& output) {
        const auto intersects = [&](const BoundingBox& box) {
            if (!direction) {
                for (std::size_t axis = 0; axis < 3; ++axis)
                    if (static_cast<long double>(coordinate(point,axis)) < coordinate(box.min,axis)-margin ||
                        static_cast<long double>(coordinate(point,axis)) > coordinate(box.max,axis)+margin) return false;
                return true;
            }
            long double first = 0, last = std::numeric_limits<long double>::infinity();
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const long double component = axis == 0 ? direction->x : axis == 1 ? direction->y : direction->z;
                const long double position = coordinate(point,axis);
                const long double lower = static_cast<long double>(coordinate(box.min,axis))-margin;
                const long double upper = static_cast<long double>(coordinate(box.max,axis))+margin;
                if (!std::isfinite(lower) || !std::isfinite(upper)) return true;
                if (component == 0) { if (position < lower || position > upper) return false; continue; }
                long double low = (lower-position)/component, high = (upper-position)/component;
                if (!std::isfinite(low) || !std::isfinite(high)) return true;
                if (low > high) std::swap(low,high);
                first = std::max(first,low); last = std::min(last,high);
                if (first > last) return false;
            }
            return last >= 0;
        };
        std::vector<int> pending;
        if (!index->nodes.empty()) pending.push_back(0);
        while (!pending.empty()) {
            const int id = pending.back(); pending.pop_back();
            if (!consume_work(work,limit)) return false;
            const auto& node = index->nodes[static_cast<std::size_t>(id)];
            if (!intersects(node.bounds)) continue;
            if (node.left >= 0) { pending.push_back(node.left); pending.push_back(node.right); continue; }
            for (std::size_t i = node.begin; i < node.end; ++i) {
                if (!consume_work(work,limit)) return false;
                const auto face = index->order[i];
                if (intersects(faces[face].bbox)) output.push_back(face);
            }
        }
        // Keep public boundary source order and the original ray face order.
        std::sort(output.begin(),output.end());
        return true;
    };
    std::vector<std::size_t> boundary_candidates;
    // This certificate only proves that skipped plane-distance arithmetic is
    // finite. It does not classify any point or bypass a tolerance-band check.
    if (index && std::isfinite(normal_scale*(point_scale+origin_scale)) &&
        normal_scale*(point_scale+origin_scale) < std::numeric_limits<Scalar>::max()/16 &&
        std::isfinite(margin)) {
        if (!candidates(nullptr,boundary_candidates))
            return reject(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"分类边界候选查询超过预算");
    } else for (std::size_t face = 0; face < faces.size(); ++face) boundary_candidates.push_back(face);
    bool uncertain = false;
    for (const auto face_index : boundary_candidates) {
        const auto& face = faces[face_index];
        if (!consume_work(work,limit))
            return reject(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"分类超过几何查询预算");
        const Scalar distance = detail::dot(face.normal,detail::subtract(point,face.origin));
        if (!std::isfinite(distance))
            return reject(StatusCode::NumericalInstability,diag_codes::kBoolNumericalFailure,"分类平面距离不可分辨");
        if (std::abs(distance) > tolerance) continue;
        const auto projected = detail::add_point_vec(point,detail::scale(face.normal,-distance));
        if (!ops_internal::boolean_point_in_face(face,projected,tolerance)) continue;
        const Scalar resolution = std::max(point_resolution(point),point_resolution(face.origin));
        if (std::abs(distance) <= resolution &&
            ops_internal::boolean_point_in_face(face,projected,resolution))
            out.boundary_faces.push_back(face.id);
        else uncertain = true;
    }
    if (!out.boundary_faces.empty()) {
        out.location = BooleanPointLocation::Boundary;
        return true;
    }
    if (uncertain)
        return reject(StatusCode::NumericalInstability,diag_codes::kBoolNumericalFailure,"分类点落在未解析的几何边界容差带内");
    int resolved = 0;
    bool inside = false;
    for (std::size_t ray = 0; ray < 6; ++ray) {
        if (index && index->invalid_directions[ray]) continue;
        const auto direction = index ? index->directions[ray] : detail::normalize(kBooleanRayDirections[ray]);
        bool valid = true;
        std::size_t crossings = 0;
        std::vector<std::size_t> ray_candidates;
        // A broad forward-error bound certifies all the original nonparallel
        // plane-intersection guards, including faces outside the ray's trim
        // candidates. If it cannot do so, use the original full face scan.
        const long double upper_parameter = 8*normal_scale*(origin_scale+point_scale)/(index ? index->min_denominators[ray] : Scalar(1));
        const long double upper_hit = 2*(point_scale+upper_parameter);
        const long double guard_bound = 1024*std::numeric_limits<Scalar>::epsilon()*normal_scale*
            (origin_scale+upper_hit+point_scale);
        const bool indexed_ray = index && std::isfinite(upper_parameter) && std::isfinite(upper_hit) &&
            std::isfinite(guard_bound) && guard_bound <= tolerance &&
            upper_hit < std::numeric_limits<Scalar>::max()/8 && std::isfinite(margin);
        if (indexed_ray) {
            // An exactly parallel support plane invalidates a ray near that
            // plane even if the trimmed face itself misses the ray's bounds.
            for (const auto face_index : index->parallel_faces[ray]) {
                if (!consume_work(work,limit))
                    return reject(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"分类平行支撑面查询超过预算");
                const auto& face = faces[face_index];
                const auto numerator = detail::dot(face.normal,detail::subtract(face.origin,point));
                if (std::abs(numerator) <= tolerance) { valid = false; break; }
            }
            if (!valid) continue;
            if (!candidates(&direction,ray_candidates))
                return reject(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"分类射线候选查询超过预算");
        } else for (std::size_t face = 0; face < faces.size(); ++face) ray_candidates.push_back(face);
        for (const auto face_index : ray_candidates) {
            const auto& face = faces[face_index];
            if (!consume_work(work,limit))
                return reject(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"分类超过几何查询预算");
            const Scalar denominator = index ? index->denominators[face_index][ray] : detail::dot(face.normal,direction);
            const Scalar numerator = detail::dot(face.normal,detail::subtract(face.origin,point));
            if (std::abs(denominator) <= 128 * std::numeric_limits<Scalar>::epsilon()) {
                if (std::abs(numerator) <= tolerance) { valid = false; break; }
                continue;
            }
            if (std::abs(denominator) <= std::sqrt(std::numeric_limits<Scalar>::epsilon())) { valid = false; break; }
            const Scalar parameter = numerator / denominator;
            if (!std::isfinite(parameter)) { valid = false; break; }
            if (parameter <= 0) continue;
            const auto hit = detail::add_point_vec(point,detail::scale(direction,parameter));
            if (!finite_point(hit) || point_resolution(hit) > tolerance ||
                std::abs(detail::dot(face.normal,detail::subtract(hit,face.origin))) > tolerance) { valid = false; break; }
            if (!ops_internal::boolean_point_in_face(face,hit,tolerance)) continue;
            if (!ops_internal::boolean_boundary_hits(state,face,hit,tolerance).empty()) { valid = false; break; }
            ++crossings;
        }
        if (!valid) continue;
        const bool ray_inside = crossings % 2 != 0;
        if (resolved && ray_inside != inside)
            return reject(StatusCode::NumericalInstability,diag_codes::kBoolNumericalFailure,"独立实体边界射线分类不一致");
        inside = ray_inside;
        if (++resolved == 2) {
            out.location = inside ? BooleanPointLocation::Inside : BooleanPointLocation::Outside;
            return true;
        }
    }
    return reject(StatusCode::NumericalInstability,diag_codes::kBoolNumericalFailure,"实体边界射线均无法避开边界退化");
}

// Clip a convex temporary cell by an infinite support line. Public preparation
// retains endpoint cross-cuts; rebuilding inserts explicit synchronized knots
// on the support edges. Both preserve the actual source region and its holes.
bool split_boolean_cell(const std::vector<Point3>& polygon, const Point3& origin, const Vec3& side,
                         std::vector<std::vector<Point3>>& output) {
    Scalar resolution = point_resolution(origin);
    std::vector<Scalar> distances;
    bool positive = false, negative = false;
    for (const auto& p : polygon) {
        resolution = std::max(resolution,point_resolution(p));
        distances.push_back(detail::dot(detail::subtract(p,origin),side));
    }
    for (auto& d : distances) {
        if (!std::isfinite(d)) return false;
        if (std::abs(d) <= resolution) d = 0;
        positive |= d > 0;
        negative |= d < 0;
    }
    if (!positive || !negative) { output.push_back(polygon); return true; }
    std::array<std::vector<Point3>,2> halves;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const std::size_t j = (i+1)%polygon.size();
        for (std::size_t half = 0; half < 2; ++half)
            if (half == 0 ? distances[i] >= 0 : distances[i] <= 0) halves[half].push_back(polygon[i]);
        if ((distances[i] > 0 && distances[j] < 0) || (distances[i] < 0 && distances[j] > 0)) {
            const Scalar fraction = distances[i] / (distances[i]-distances[j]);
            const auto p = detail::add_point_vec(polygon[i],detail::scale(detail::subtract(polygon[j],polygon[i]),fraction));
            if (!finite_point(p)) return false;
            for (auto& half : halves) half.push_back(p);
        }
    }
    for (auto& half : halves) {
        if (half.size() < 3) return false;
        output.push_back(std::move(half));
    }
    return true;
}

bool point_on_segment(const Point3& p, const Point3& a, const Point3& b, Scalar tolerance, Scalar& fraction) {
    const auto delta = detail::subtract(b,a);
    const Scalar length2 = detail::dot(delta,delta);
    if (!(length2 > 0)) return same_point(p,a);
    fraction = detail::dot(detail::subtract(p,a),delta)/length2;
    const Scalar fraction_resolution = tolerance / std::sqrt(length2);
    if (fraction < -fraction_resolution || fraction > 1+fraction_resolution) return false;
    fraction = std::clamp(fraction,Scalar(0),Scalar(1));
    return detail::norm(detail::subtract(p,detail::add_point_vec(a,detail::scale(delta,fraction)))) <= tolerance;
}

} // namespace

Result<std::vector<BooleanPointClassification>> BooleanService::classify_points(
    BodyId body, std::span<const Point3> points, const BooleanIntersectionOptions& options) const {
    const auto fail = [&](const BooleanReadFailure& failure) {
        auto issue = detail::make_error_issue(failure.code,failure.message,{body.value});
        issue.stage = "bool.classify";
        issue.numeric_evidence = {{"point_count",static_cast<Scalar>(points.size()),"count"},
            {"max_face_pairs",static_cast<Scalar>(options.max_face_pairs),"count"},
            {"max_segments",static_cast<Scalar>(options.max_segments),"count"},
            {"linear_tolerance",std::isfinite(options.tolerance.linear) ? options.tolerance.linear : 0,"model_unit"},
            {"linear_tolerance_finite",std::isfinite(options.tolerance.linear) ? 1.0 : 0.0,"bool"}};
        return error_result<std::vector<BooleanPointClassification>>(failure.status,
            state_->create_diagnostic("布尔实体点分类失败",{std::move(issue)}));
    };
    std::vector<BooleanPlanarFace> faces;
    BooleanReadFailure failure;
    std::size_t work = 0;
    if (!read_classification_body(*state_,body,options,faces,failure,work)) return fail(failure);
    if (points.size() > options.max_segments)
        return fail({StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"分类点数量超过预算"});
    std::vector<BooleanPointClassification> result;
    for (const auto& point : points) {
        BooleanPointClassification classification;
        if (!classify_boolean_point(*state_,faces,nullptr,point,options.tolerance.linear,work,options.max_face_pairs,
                                    classification,failure)) return fail(failure);
        result.push_back(std::move(classification));
    }
    return ok_result(std::move(result));
}

Result<BooleanSplitClassificationPreparation> BooleanService::prepare_split_classification(
    BodyId lhs, BodyId rhs, const BooleanSplitClassificationOptions& options) const {
    return prepare_split_classification_impl(lhs,rhs,options,false);
}

Result<BooleanSplitClassificationPreparation> BooleanService::prepare_split_classification_impl(
    BodyId lhs, BodyId rhs, const BooleanSplitClassificationOptions& options, bool resolve_coplanar) const {
    std::size_t work = 0, cell_count = 0, vertex_count = 0, index_nodes = 0;
    std::size_t work_after_split = 0, work_after_sync = 0, work_after_coverage = 0;
    const auto fail = [&](StatusCode status, std::string_view code, std::string_view stage, const char* message,
                          FaceId face = {}) {
        auto issue = detail::make_error_issue(code,message,{lhs.value,rhs.value});
        if (face.value) issue.related_entities.push_back(face.value);
        issue.stage = std::string(stage);
        issue.numeric_evidence = {{"consumed_work",static_cast<Scalar>(work),"count"},
            {"source_cells",static_cast<Scalar>(cell_count),"count"},
            {"sync_points",static_cast<Scalar>(vertex_count),"count"},
            {"sync_index_nodes",static_cast<Scalar>(index_nodes),"count"},
            {"work_after_split",static_cast<Scalar>(work_after_split),"count"},
            {"work_after_sync",static_cast<Scalar>(work_after_sync),"count"},
            {"work_after_coverage",static_cast<Scalar>(work_after_coverage),"count"},
            {"max_fragments",static_cast<Scalar>(options.max_fragments),"count"},
            {"max_face_pairs",static_cast<Scalar>(options.intersection.max_face_pairs),"count"},
            {"linear_tolerance",std::isfinite(options.intersection.tolerance.linear) ? options.intersection.tolerance.linear : 0,"model_unit"},
            {"angular_tolerance",std::isfinite(options.intersection.tolerance.angular) ? options.intersection.tolerance.angular : 0,"radian"},
            {"linear_tolerance_finite",std::isfinite(options.intersection.tolerance.linear) ? 1.0 : 0.0,"bool"},
            {"angular_tolerance_finite",std::isfinite(options.intersection.tolerance.angular) ? 1.0 : 0.0,"bool"}};
        return error_result<BooleanSplitClassificationPreparation>(status,
            state_->create_diagnostic("布尔真实切分与分类准备失败",{std::move(issue)}));
    };
    if (options.max_fragments == 0 || options.max_fragments > 100000)
        return fail(StatusCode::InvalidInput,diag_codes::kBoolInvalidInput,"bool.split","切分片预算无效");
    auto intersection = ops_internal::prepare_planar_boolean_intersections(*state_,lhs,rhs,options.intersection,resolve_coplanar);
    if (intersection.status != StatusCode::Ok || !intersection.value)
        return error_result<BooleanSplitClassificationPreparation>(intersection.status,intersection.diagnostic_id,intersection.warnings);
    BooleanSplitClassificationPreparation result;
    result.intersection = std::move(*intersection.value);
    std::array<std::vector<BooleanPlanarFace>,2> faces;
    const std::array<BodyId,2> bodies {lhs,rhs};
    BooleanReadFailure failure;
    for (std::size_t side = 0; side < 2; ++side)
        if (!read_classification_body(*state_,bodies[side],options.intersection,faces[side],failure,work))
            return fail(failure.status,failure.code,"bool.classify",failure.message);
    const Scalar tolerance = options.intersection.tolerance.linear;
    const std::size_t limit = options.intersection.max_face_pairs;
    struct Cell { std::size_t side {}; FaceId face {}; std::vector<Point3> vertices; };
    std::vector<Cell> cells;
    const std::size_t operand_count = resolve_coplanar && lhs == rhs ? 1 : 2;
    for (std::size_t side = 0; side < operand_count; ++side) for (const auto& face : faces[side]) {
        std::vector<Point3> points = face.rings.front();
        std::vector<std::array<int,3>> triangles;
        if (face.rings.size() == 1) {
            if (!detail::triangulate_extrude_profile(points,face.normal,triangles,true))
                return fail(StatusCode::InvalidTopology,diag_codes::kBoolSplitFailure,"bool.split","真实面三角化失败",face.id);
        } else {
            const std::vector<std::vector<Point3>> holes(face.rings.begin()+1,face.rings.end());
            std::vector<std::pair<int,int>> boundary;
            if (!detail::triangulate_extrude_region(face.rings.front(),holes,face.normal,tolerance,points,boundary,triangles,true))
                return fail(StatusCode::InvalidTopology,diag_codes::kBoolSplitFailure,"bool.split","真实带孔面三角化失败",face.id);
        }
        std::vector<std::vector<Point3>> polygons;
        for (const auto& tri : triangles) polygons.push_back({points[tri[0]],points[tri[1]],points[tri[2]]});
        std::vector<std::pair<Point3,Vec3>> applied_cuts;
        for (const auto& segment : result.intersection.segments) {
            if (segment.lhs_face != face.id && segment.rhs_face != face.id) continue;
            auto direction = segment.point_contact ? face.u : detail::normalize(detail::subtract(segment.end,segment.begin));
            if (!segment.point_contact) {
                // Short finite segments amplify endpoint roundoff when used to
                // reconstruct an infinite support line. Use the actual source
                // planes or a shared source edge instead; do not snap by the
                // user's geometric tolerance or discard the resulting pieces.
                const auto other_id = segment.lhs_face == face.id ? segment.rhs_face : segment.lhs_face;
                const BooleanPlanarFace* other = nullptr;
                for (const auto& candidate : faces[1-side]) {
                    if (!consume_work(work,limit))
                        return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                                    "bool.split","切分支撑方向恢复超过几何工作预算",face.id);
                    if (candidate.id == other_id) { other = &candidate; break; }
                }
                if (other) {
                    const auto axis = detail::cross(face.normal,other->normal);
                    if (detail::norm(axis) > 64 * std::numeric_limits<Scalar>::epsilon())
                        direction = detail::normalize(axis);
                    else {
                        bool found = false;
                        for (const auto& begin_hit : segment.begin_hits) {
                            for (const auto& end_hit : segment.end_hits) {
                                if (!consume_work(work,limit))
                                    return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                                                "bool.split","切分源边方向恢复超过几何工作预算",face.id);
                                if (begin_hit.edge != end_hit.edge) continue;
                                const auto& edge = state_->edges.at(begin_hit.edge.value);
                                const auto& a = state_->vertices.at(edge.v0.value).point;
                                const auto& b = state_->vertices.at(edge.v1.value).point;
                                direction = detail::normalize(detail::subtract(b,a));
                                found = true;
                                break;
                            }
                            if (found) break;
                        }
                    }
                }
                if (detail::dot(direction,detail::subtract(segment.end,segment.begin)) < 0)
                    direction = detail::scale(direction,-1);
            }
            const std::array<Point3,3> anchors {segment.begin,segment.begin,segment.end};
            const std::array<Vec3,3> sides {detail::normalize(detail::cross(face.normal,direction)),direction,direction};
            for (std::size_t cut = 0; cut < (segment.point_contact ? 2U : 3U); ++cut) {
                // The real support cut makes every finite segment endpoint a
                // point on a cell edge. The synchronized knots below split that
                // edge at both endpoints, including endpoints inside the face;
                // perpendicular cuts would only propagate artificial seams.
                if (resolve_coplanar && !segment.point_contact && cut > 0) continue;
                // A contact on the source boundary already has a boundary edge
                // for its synchronized knot; only interior point contacts need
                // a through-point cut to create an edge for that knot.
                const auto& hits = cut == 2 ? segment.end_hits : segment.begin_hits;
                if (resolve_coplanar && segment.point_contact) {
                    if (!consume_work(work,limit,hits.size()))
                        return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                                    "bool.split","有限交段边界来源查询超过几何工作预算",face.id);
                    if (std::any_of(hits.begin(),hits.end(),[&](const auto& hit) {
                        return hit.face == face.id;
                    })) continue;
                }
                bool duplicate = false;
                for (const auto& [old_anchor,old_side] : applied_cuts) {
                    if (!consume_work(work,limit))
                        return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                                    "bool.split","切分支撑线去重超过几何工作预算",face.id);
                    if (detail::norm(detail::cross(old_side,sides[cut])) <=
                            64 * std::numeric_limits<Scalar>::epsilon() &&
                        std::abs(detail::dot(old_side,detail::subtract(anchors[cut],old_anchor))) <=
                            std::max(point_resolution(old_anchor),point_resolution(anchors[cut]))) {
                        duplicate = true;
                        break;
                    }
                }
                // Repeated source-pair constraints still retain their finite
                // endpoints, provenance and coverage checks below. Applying
                // the identical infinite cut again adds no geometric information.
                if (duplicate) continue;
                applied_cuts.emplace_back(anchors[cut],sides[cut]);
                std::vector<std::vector<Point3>> next;
                for (const auto& polygon : polygons) {
                    if (!consume_work(work,limit,polygon.size()))
                        return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"bool.split","切分超过几何工作预算",face.id);
                    if (!split_boolean_cell(polygon,anchors[cut],sides[cut],next))
                        return fail(StatusCode::NumericalInstability,diag_codes::kBoolSplitFailure,"bool.split","切分支撑线产生不可分辨面片",face.id);
                    if (next.size() > options.max_fragments)
                        return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"bool.split","切分超过面片预算",face.id);
                }
                polygons = std::move(next);
            }
        }
        for (auto& polygon : polygons) cells.push_back({side,face.id,std::move(polygon)});
        cell_count = cells.size();
        if (cells.size() > options.max_fragments)
            return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"bool.split","切分超过总面片预算",face.id);
    }
    work_after_split = work;
    // Insert all incident subdivision points before triangulating. This also
    // synchronizes source-edge cuts between adjacent faces, avoiding T-junctions.
    std::array<std::vector<Point3>,2> vertices;
    for (const auto& cell : cells) for (const auto& p : cell.vertices) vertices[cell.side].push_back(p);
    if (resolve_coplanar) {
        for (const auto& segment : result.intersection.segments) {
            for (std::size_t side = 0; side < operand_count; ++side) {
                if (!consume_work(work,limit,2))
                    return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                                "bool.split","有限交段端点同步超过几何工作预算");
                vertices[side].push_back(segment.begin);
                vertices[side].push_back(segment.end);
            }
        }
    }
    for (auto& points : vertices) {
        std::sort(points.begin(),points.end(),[](const auto& a,const auto& b) {
            if (a.x != b.x) return a.x < b.x;
            if (a.y != b.y) return a.y < b.y;
            return a.z < b.z;
        });
        points.erase(std::unique(points.begin(),points.end(),same_point),points.end());
        vertex_count += points.size();
    }
    // Only immutable temporary knots enter this local spatial index. A
    // hierarchy of three-dimensional bounds avoids revisiting whole planar
    // point slabs on every synchronized edge of a repeated Boolean result.
    const auto coordinate = [](const Point3& point, std::size_t axis) {
        return axis == 0 ? point.x : axis == 1 ? point.y : point.z;
    };
    struct PointIndexNode {
        BoundingBox bounds{};
        std::size_t begin{}, end{};
        int left{-1}, right{-1};
    };
    std::array<std::vector<const Point3*>,2> indexed_points;
    std::array<std::vector<PointIndexNode>,2> point_indexes;
    const auto build_index = [&](auto&& self, std::size_t side,
                                 std::size_t begin, std::size_t end) -> int {
        PointIndexNode node;
        node.begin = begin; node.end = end;
        for (std::size_t i = begin; i < end; ++i) {
            if (!consume_work(work,limit)) return -1;
            detail::extend_materialization_bbox(node.bounds,*indexed_points[side][i]);
        }
        const int index = static_cast<int>(point_indexes[side].size());
        point_indexes[side].push_back(node);
        ++index_nodes;
        std::size_t axis = 0;
        for (std::size_t candidate = 1; candidate < 3; ++candidate)
            if (coordinate(node.bounds.max,candidate)-coordinate(node.bounds.min,candidate) >
                coordinate(node.bounds.max,axis)-coordinate(node.bounds.min,axis)) axis = candidate;
        if (end-begin > 8) {
            const std::size_t middle = begin+(end-begin)/2;
            auto& points = indexed_points[side];
            std::nth_element(points.begin()+static_cast<std::ptrdiff_t>(begin),
                points.begin()+static_cast<std::ptrdiff_t>(middle),
                points.begin()+static_cast<std::ptrdiff_t>(end),
                [&](const Point3* a,const Point3* b) {
                    return coordinate(*a,axis) < coordinate(*b,axis);
                });
            node.left = self(self,side,begin,middle);
            if (node.left < 0) return -1;
            node.right = self(self,side,middle,end);
            if (node.right < 0) return -1;
            point_indexes[side][static_cast<std::size_t>(index)] = node;
        }
        return index;
    };
    for (std::size_t side = 0; side < operand_count; ++side) {
        for (const auto& point : vertices[side]) indexed_points[side].push_back(&point);
        if (!indexed_points[side].empty() &&
            build_index(build_index,side,0,indexed_points[side].size()) < 0)
            return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                        "bool.split","同步点空间索引构建超过几何工作预算");
    }
    const auto point_candidates = [&](std::size_t side, const Point3& a, const Point3& b,
                                      Scalar resolution, std::vector<const Point3*>& candidates) {
        std::array<Scalar,3> lower{}, upper{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            lower[axis] = std::min(coordinate(a,axis),coordinate(b,axis))-resolution;
            upper[axis] = std::max(coordinate(a,axis),coordinate(b,axis))+resolution;
        }
        std::vector<int> pending;
        if (!point_indexes[side].empty()) pending.push_back(0);
        while (!pending.empty()) {
            const int index = pending.back(); pending.pop_back();
            if (!consume_work(work,limit)) return false;
            const auto& node = point_indexes[side][static_cast<std::size_t>(index)];
            bool overlaps = true;
            for (std::size_t axis = 0; axis < 3; ++axis)
                overlaps &= coordinate(node.bounds.max,axis) >= lower[axis] &&
                    coordinate(node.bounds.min,axis) <= upper[axis];
            if (!overlaps) continue;
            if (node.left >= 0) {
                pending.push_back(node.left); pending.push_back(node.right);
                continue;
            }
            for (std::size_t i = node.begin; i < node.end; ++i) {
                if (!consume_work(work,limit)) return false;
                const auto* point = indexed_points[side][i];
                bool in_bounds = true;
                for (std::size_t axis = 0; axis < 3; ++axis)
                    in_bounds &= coordinate(*point,axis) >= lower[axis] &&
                        coordinate(*point,axis) <= upper[axis];
                if (in_bounds) candidates.push_back(point);
            }
        }
        return true;
    };
    for (auto& cell : cells) {
        std::vector<Point3> refined;
        for (std::size_t i = 0; i < cell.vertices.size(); ++i) {
            const auto& a = cell.vertices[i];
            const auto& b = cell.vertices[(i+1)%cell.vertices.size()];
            std::vector<std::pair<Scalar,Point3>> knots {{0,a}};
            const Scalar resolution = std::max(point_resolution(a),point_resolution(b));
            std::vector<const Point3*> candidates;
            if (!point_candidates(cell.side,a,b,resolution,candidates))
                return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                            "bool.split","邻接同步超过几何工作预算",cell.face);
            for (const auto* candidate : candidates) {
                if (!consume_work(work,limit))
                    return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"bool.split","邻接同步超过几何工作预算",cell.face);
                const auto& p = *candidate;
                Scalar fraction = 0;
                if (point_on_segment(p,a,b,resolution,fraction) &&
                    fraction > 0 && fraction < 1) knots.push_back({fraction,p});
            }
            std::sort(knots.begin(),knots.end(),[](const auto& a,const auto& b) { return a.first < b.first; });
            for (const auto& knot : knots)
                if (refined.empty() || !same_point(refined.back(),knot.second)) refined.push_back(knot.second);
        }
        if (refined.size() > 1 && same_point(refined.front(),refined.back())) refined.pop_back();
        Point3 center = refined.front();
        Vec3 sum {};
        for (const auto& p : refined) {
            const auto offset = detail::subtract(p,center);
            sum.x += offset.x; sum.y += offset.y; sum.z += offset.z;
        }
        center = detail::add_point_vec(center,detail::scale(sum,1/static_cast<Scalar>(refined.size())));
        const auto fit = std::find_if(faces[cell.side].begin(),faces[cell.side].end(),[&](const auto& f) { return f.id == cell.face; });
        // A cell that is already a triangle has all synchronized boundary
        // nodes. Adding a centroid there contributes no constraint and triples
        // its owned faces on every subsequent Boolean operation.
        const std::size_t triangle_count = refined.size() == 3 ? 1 : refined.size();
        for (std::size_t i = 0; i < triangle_count; ++i) {
            BooleanFaceFragment fragment;
            fragment.source_body = bodies[cell.side];
            fragment.source_face = cell.face;
            fragment.vertices = refined.size() == 3 ?
                std::array<Point3,3>{refined[0],refined[1],refined[2]} :
                std::array<Point3,3>{center,refined[i],refined[(i+1)%refined.size()]};
            const auto cross = detail::cross(
                detail::subtract(fragment.vertices[1],fragment.vertices[0]),
                detail::subtract(fragment.vertices[2],fragment.vertices[0]));
            const Scalar area2 = detail::norm(cross);
            if (!std::isfinite(area2) || !(area2 > point_resolution(center)*
                detail::norm(detail::subtract(fragment.vertices[2],fragment.vertices[1]))))
                return fail(StatusCode::NumericalInstability,diag_codes::kBoolSplitFailure,"bool.split","三角切分片面积无法可靠分辨",cell.face);
            Vec3 outer_normal {};
            const auto& anchor = fit->rings.front().front();
            for (std::size_t k = 1; k+1 < fit->rings.front().size(); ++k) {
                const auto term = detail::cross(detail::subtract(fit->rings.front()[k],anchor),
                    detail::subtract(fit->rings.front()[k+1],anchor));
                outer_normal.x += term.x; outer_normal.y += term.y; outer_normal.z += term.z;
            }
            if (detail::dot(cross,outer_normal) < 0) std::swap(fragment.vertices[1],fragment.vertices[2]);
            for (std::size_t e = 0; e < 3; ++e) {
                const auto begin_hits = ops_internal::boolean_boundary_hits(*state_,*fit,fragment.vertices[e],point_resolution(fragment.vertices[e]));
                const auto end_hits = ops_internal::boolean_boundary_hits(*state_,*fit,fragment.vertices[(e+1)%3],point_resolution(fragment.vertices[(e+1)%3]));
                for (const auto& begin_hit : begin_hits) for (const auto& end_hit : end_hits)
                    if (begin_hit.edge == end_hit.edge) {
                        fragment.source_edges[e] = begin_hit.edge;
                        fragment.source_edge_begin[e] = begin_hit.edge_fraction;
                        fragment.source_edge_end[e] = end_hit.edge_fraction;
                    }
            }
            for (std::size_t s = 0; s < result.intersection.segments.size(); ++s) {
                const auto& segment = result.intersection.segments[s];
                if (segment.lhs_face != cell.face && segment.rhs_face != cell.face) continue;
                bool incident = false;
                for (const auto& p : fragment.vertices) {
                    Scalar fraction = 0;
                    incident |= segment.point_contact ? same_point(p,segment.begin) :
                        point_on_segment(p,segment.begin,segment.end,std::max(point_resolution(p),point_resolution(segment.begin)),fraction);
                }
                if (incident) fragment.intersection_segments.push_back(s);
            }
            result.fragments.push_back(std::move(fragment));
            if (result.fragments.size() > options.max_fragments)
                return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"bool.split","三角切分超过总面片预算",cell.face);
        }
    }
    work_after_sync = work;
    // Independently check that both source faces contain the entire finite
    // constraint as subdivision edges, not merely as incident metadata. Index
    // immutable edge starts per source face so small finite segments do not
    // repeatedly query every unrelated triangle of a large source face.
    struct FragmentEdge {
        const Point3* begin{};
        const Point3* end{};
        std::size_t fragment{}, side{};
    };
    std::vector<FragmentEdge> fragment_edges;
    std::map<std::uint64_t,std::array<std::vector<std::size_t>,3>> face_edge_indexes;
    std::map<std::uint64_t,Scalar> face_point_resolutions;
    std::map<std::uint64_t,std::array<std::vector<std::size_t>,3>> body_edge_indexes;
    std::map<std::uint64_t,Scalar> body_point_resolutions;
    for (std::size_t fragment_index = 0; fragment_index < result.fragments.size(); ++fragment_index) {
        const auto& fragment = result.fragments[fragment_index];
        for (std::size_t edge = 0; edge < 3; ++edge) {
            if (!consume_work(work,limit))
                return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                            "bool.split","交段覆盖边索引构建超过预算",fragment.source_face);
            const auto index = fragment_edges.size();
            fragment_edges.push_back({&fragment.vertices[edge],&fragment.vertices[(edge+1)%3],fragment_index,edge});
            for (auto& axis : face_edge_indexes[fragment.source_face.value]) axis.push_back(index);
            for (auto& axis : body_edge_indexes[fragment.source_body.value]) axis.push_back(index);
            auto& resolution = face_point_resolutions[fragment.source_face.value];
            resolution = std::max(resolution,point_resolution(fragment.vertices[edge]));
            auto& body_resolution = body_point_resolutions[fragment.source_body.value];
            body_resolution = std::max(body_resolution,point_resolution(fragment.vertices[edge]));
        }
    }
    for (auto& [face,indexes] : face_edge_indexes) {
        (void)face;
        for (std::size_t axis = 0; axis < 3; ++axis)
            std::sort(indexes[axis].begin(),indexes[axis].end(),[&](std::size_t a,std::size_t b) {
                return coordinate(*fragment_edges[a].begin,axis) < coordinate(*fragment_edges[b].begin,axis);
            });
    }
    using ConstraintKey = std::pair<std::uint64_t,std::array<Scalar,7>>;
    std::map<ConstraintKey,bool> certified_constraints;
    for (const auto& segment : result.intersection.segments) {
        for (const auto face : {segment.lhs_face,segment.rhs_face}) {
            auto begin = std::array{segment.begin.x,segment.begin.y,segment.begin.z};
            auto end = std::array{segment.end.x,segment.end.y,segment.end.z};
            if (end < begin) std::swap(begin,end);
            const ConstraintKey key {face.value,{begin[0],begin[1],begin[2],end[0],end[1],end[2],
                                                segment.point_contact ? Scalar(1) : Scalar(0)}};
            if (!consume_work(work,limit))
                return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                            "bool.split","交段约束证书查询超过预算",face);
            if (certified_constraints.contains(key)) continue;
            const Scalar length = detail::norm(detail::subtract(segment.end,segment.begin));
            const Scalar resolution = std::max(point_resolution(segment.begin),point_resolution(segment.end));
            std::vector<std::pair<Scalar,Scalar>> intervals;
            bool point_found = false;
            const auto& indexes = face_edge_indexes.at(face.value);
            auto first = indexes[0].cbegin(), last = indexes[0].cend();
            const Scalar candidate_resolution = segment.point_contact ?
                std::max(resolution,face_point_resolutions.at(face.value)) : resolution;
            std::array<Scalar,3> lower{}, upper{};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                lower[axis] = std::min(coordinate(segment.begin,axis),coordinate(segment.end,axis))-candidate_resolution;
                upper[axis] = std::max(coordinate(segment.begin,axis),coordinate(segment.end,axis))+candidate_resolution;
                const auto& index = indexes[axis];
                const auto low = std::lower_bound(index.cbegin(),index.cend(),lower[axis],
                    [&](std::size_t edge,Scalar value) {
                        return coordinate(*fragment_edges[edge].begin,axis) < value;
                    });
                const auto high = std::upper_bound(low,index.cend(),upper[axis],
                    [&](Scalar value,std::size_t edge) {
                        return value < coordinate(*fragment_edges[edge].begin,axis);
                    });
                if (high-low < last-first) { first = low; last = high; }
            }
            for (auto candidate = first; candidate != last; ++candidate) {
                if (!consume_work(work,limit))
                    return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                                "bool.split","交段约束验证超过预算",face);
                const auto& edge = fragment_edges[*candidate];
                const auto& a = *edge.begin;
                const auto& b = *edge.end;
                bool in_bounds = true;
                for (std::size_t axis = 0; axis < 3; ++axis)
                    in_bounds &= coordinate(a,axis) >= lower[axis] && coordinate(a,axis) <= upper[axis];
                if (!in_bounds) continue;
                if (segment.point_contact) {
                    if (!consume_work(work,limit))
                        return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                                    "bool.split","相切点约束验证超过预算",face);
                    point_found |= same_point(a,segment.begin);
                    continue;
                }
                for (std::size_t axis = 0; axis < 3; ++axis)
                    in_bounds &= coordinate(b,axis) >= lower[axis] && coordinate(b,axis) <= upper[axis];
                if (!in_bounds) continue;
                Scalar u = 0, v = 0;
                if (!consume_work(work,limit,2))
                    return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                                "bool.split","有限交段端点约束验证超过预算",face);
                if (point_on_segment(a,segment.begin,segment.end,resolution,u) &&
                    point_on_segment(b,segment.begin,segment.end,resolution,v) && std::abs(u-v)*length > resolution)
                    intervals.push_back({std::min(u,v),std::max(u,v)});
            }
            if (segment.point_contact) {
                if (!point_found)
                    return fail(StatusCode::NumericalInstability,diag_codes::kBoolSplitFailure,"bool.split","相切点未保留为真实切分顶点",face);
                certified_constraints.emplace(key,true);
                continue;
            }
            std::sort(intervals.begin(),intervals.end());
            Scalar covered = 0;
            for (const auto& interval : intervals) {
                if ((interval.first-covered)*length > resolution)
                    return fail(StatusCode::NumericalInstability,diag_codes::kBoolSplitFailure,"bool.split","真实交段切分边存在间隙",face);
                covered = std::max(covered,interval.second);
            }
            if ((1-covered)*length > resolution)
                return fail(StatusCode::NumericalInstability,diag_codes::kBoolSplitFailure,"bool.split","真实交段未被切分边完整覆盖",face);
            certified_constraints.emplace(key,true);
        }
    }
    work_after_coverage = work;
    // Find only potential reverse uses within the original machine-scale
    // endpoint predicate. Exact coordinate keys would miss valid roundoff
    // matches; searching a conservative box keeps the same geometric test.
    for (auto& [body,indexes] : body_edge_indexes) {
        (void)body;
        for (std::size_t axis = 0; axis < 3; ++axis)
            std::sort(indexes[axis].begin(),indexes[axis].end(),[&](std::size_t a,std::size_t b) {
                return coordinate(*fragment_edges[a].begin,axis) < coordinate(*fragment_edges[b].begin,axis);
            });
    }
    std::vector<std::array<std::size_t,3>> edge_neighbors(result.fragments.size());
    for (std::size_t a = 0; a < result.fragments.size(); ++a) {
        const auto body = result.fragments[a].source_body.value;
        const auto& indexes = body_edge_indexes.at(body);
        const Scalar resolution = body_point_resolutions.at(body);
        for (std::size_t i = 0; i < 3; ++i) {
            const auto& edge = fragment_edges[3*a+i];
            auto first = indexes[0].cbegin(), last = indexes[0].cend();
            std::array<Scalar,3> lower{}, upper{};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                lower[axis] = coordinate(*edge.end,axis)-resolution;
                upper[axis] = coordinate(*edge.end,axis)+resolution;
                const auto& index = indexes[axis];
                const auto low = std::lower_bound(index.cbegin(),index.cend(),lower[axis],
                    [&](std::size_t candidate,Scalar value) {
                        return coordinate(*fragment_edges[candidate].begin,axis) < value;
                    });
                const auto high = std::upper_bound(low,index.cend(),upper[axis],
                    [&](Scalar value,std::size_t candidate) {
                        return value < coordinate(*fragment_edges[candidate].begin,axis);
                    });
                if (high-low < last-first) { first = low; last = high; }
            }
            for (auto candidate = first; candidate != last; ++candidate) {
                if (!consume_work(work,limit))
                    return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                                "bool.split","邻接计算超过几何工作预算");
                const auto& other = fragment_edges[*candidate];
                if (other.fragment <= a) continue;
                bool in_bounds = true;
                for (std::size_t axis = 0; axis < 3; ++axis)
                    in_bounds &= coordinate(*other.begin,axis) >= lower[axis] &&
                        coordinate(*other.begin,axis) <= upper[axis];
                if (!in_bounds) continue;
                if (!consume_work(work,limit))
                    return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                                "bool.split","邻接端点认证超过几何工作预算");
                if (!same_point(*edge.end,*other.begin)) continue;
                if (!consume_work(work,limit))
                    return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                                "bool.split","邻接端点认证超过几何工作预算");
                if (!same_point(*edge.begin,*other.end)) continue;
                ++edge_neighbors[a][i];
                ++edge_neighbors[other.fragment][other.side];
                result.fragments[a].adjacent_fragments.push_back(other.fragment);
                result.fragments[other.fragment].adjacent_fragments.push_back(a);
            }
        }
    }
    for (auto& fragment : result.fragments) {
        auto& adjacent = fragment.adjacent_fragments;
        std::sort(adjacent.begin(),adjacent.end());
        adjacent.erase(std::unique(adjacent.begin(),adjacent.end()),adjacent.end());
    }
    for (std::size_t i = 0; i < edge_neighbors.size(); ++i)
        if (std::any_of(edge_neighbors[i].begin(),edge_neighbors[i].end(),[](std::size_t n) { return n != 1; }))
            return fail(StatusCode::NumericalInstability,diag_codes::kBoolSplitFailure,"bool.split",
                "切分边未形成唯一反向整边邻接",result.fragments[i].source_face);
    for (std::size_t side = 0; side < operand_count; ++side) {
        std::map<std::uint64_t,std::vector<FaceId>> edge_faces;
        for (const auto& face : faces[side]) for (const auto& ring : face.edges) for (const auto edge : ring)
            edge_faces[edge.value].push_back(face.id);
        for (const auto& [raw,incident_faces] : edge_faces) {
            const auto& edge = state_->edges.at(raw);
            const auto& a = state_->vertices.at(edge.v0.value).point;
            const auto& b = state_->vertices.at(edge.v1.value).point;
            const auto delta = detail::subtract(b,a);
            const Scalar length = detail::norm(delta);
            std::vector<Scalar> knots {0,1};
            const Scalar resolution = std::max(point_resolution(a),point_resolution(b));
            std::vector<const Point3*> candidates;
            if (!point_candidates(side,a,b,resolution,candidates))
                return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                            "bool.split","源边切分超过几何工作预算");
            for (const auto* candidate : candidates) {
                if (!consume_work(work,limit))
                    return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"bool.split","源边切分超过几何工作预算");
                const auto& p = *candidate;
                Scalar fraction = 0;
                if (point_on_segment(p,a,b,resolution,fraction)) knots.push_back(fraction);
            }
            std::sort(knots.begin(),knots.end());
            knots.erase(std::unique(knots.begin(),knots.end(),[&](Scalar x,Scalar y) {
                return (y-x)*length <= std::max(point_resolution(a),point_resolution(b));
            }),knots.end());
            for (std::size_t i = 1; i < knots.size(); ++i) {
                BooleanEdgeFragment fragment;
                fragment.source_body = bodies[side];
                fragment.source_edge = EdgeId {raw};
                fragment.begin_fraction = knots[i-1];
                fragment.end_fraction = knots[i];
                fragment.begin = detail::add_point_vec(a,detail::scale(delta,knots[i-1]));
                fragment.end = detail::add_point_vec(a,detail::scale(delta,knots[i]));
                fragment.adjacent_faces = incident_faces;
                result.edge_fragments.push_back(std::move(fragment));
                if (result.fragments.size()+result.edge_fragments.size() > options.max_fragments)
                    return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"bool.split","面边切分片合计超过预算");
            }
        }
    }
    std::array<BooleanClassificationIndex,2> classification_indexes;
    for (std::size_t side = 0; side < 2; ++side)
        if (!build_classification_index(faces[side],classification_indexes[side],work,limit))
            return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                        "bool.classify","分类空间索引构建超过预算");
    for (auto& fragment : result.fragments) {
        const std::size_t side = fragment.source_body == lhs ? 0 : 1;
        const auto& a = fragment.vertices[0];
        const auto u = detail::subtract(fragment.vertices[1],a);
        const auto v = detail::subtract(fragment.vertices[2],a);
        const auto center = detail::add_point_vec(a,detail::scale(Vec3 {u.x+v.x,u.y+v.y,u.z+v.z},Scalar(1)/3));
        if (!classify_boolean_point(*state_,faces[1-side],&classification_indexes[1-side],center,tolerance,work,limit,fragment.classification,failure))
            return fail(failure.status,failure.code,"bool.classify",failure.message,fragment.source_face);
    }
    return ok_result(std::move(result));
}

} // namespace axiom
