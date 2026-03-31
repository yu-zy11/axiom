#pragma once

#include <memory>
#include <string>
#include <span>
#include <string_view>
#include <unordered_map>

#include "axiom/core/result.h"

namespace axiom {
namespace detail {
struct KernelState;
}

class EvalGraphService {
public:
    explicit EvalGraphService(std::shared_ptr<detail::KernelState> state);

    Result<NodeId> register_node(NodeKind kind, std::string_view label);
    Result<void> add_dependency(NodeId from, NodeId to);
    Result<void> invalidate(NodeId node_id);
    Result<void> invalidate_body(BodyId body_id);
    Result<void> recompute(NodeId node_id);
    Result<bool> is_invalid(NodeId node_id) const;
    Result<std::uint64_t> recompute_count(NodeId node_id) const;
    Result<std::vector<NodeId>> dependencies_of(NodeId node_id) const;
    Result<std::vector<NodeId>> dependents_of(NodeId node_id) const;
    Result<bool> exists(NodeId node_id) const;
    Result<NodeKind> kind_of(NodeId node_id) const;
    Result<std::string> label_of(NodeId node_id) const;
    Result<void> set_label(NodeId node_id, std::string_view label);
    Result<std::uint64_t> node_count() const;
    Result<std::uint64_t> invalid_node_count() const;
    Result<std::uint64_t> valid_node_count() const;
    Result<std::uint64_t> dependency_count(NodeId node_id) const;
    Result<std::uint64_t> dependent_count(NodeId node_id) const;
    Result<bool> has_dependency(NodeId from, NodeId to) const;
    Result<void> remove_dependency(NodeId from, NodeId to);
    Result<void> clear_dependencies(NodeId node_id);
    Result<void> clear_dependents(NodeId node_id);
    Result<void> reset_recompute_count(NodeId node_id);
    Result<void> reset_all_recompute_counts();
    Result<std::uint64_t> total_recompute_count() const;
    Result<std::vector<NodeId>> all_nodes() const;
    Result<std::vector<NodeId>> invalid_nodes() const;
    Result<std::vector<NodeId>> valid_nodes() const;
    Result<std::vector<NodeId>> nodes_of_kind(NodeKind kind) const;
    Result<std::vector<std::string>> labels_of_nodes(std::span<const NodeId> nodes) const;
    Result<std::vector<NodeId>> find_by_label_token(std::string_view token, std::uint64_t max_results) const;
    Result<void> invalidate_many(std::span<const NodeId> nodes);
    Result<void> recompute_many(std::span<const NodeId> nodes);
    Result<void> remove_node(NodeId node_id);
    Result<void> clear_graph();
    Result<std::uint64_t> body_binding_count(BodyId body_id) const;
    Result<std::vector<NodeId>> nodes_of_body(BodyId body_id) const;
    Result<bool> is_leaf(NodeId node_id) const;
    Result<bool> is_root(NodeId node_id) const;
    Result<std::vector<NodeId>> ids_sorted_asc() const;
    Result<std::vector<NodeId>> ids_sorted_desc() const;
    Result<double> invalid_ratio() const;
    Result<std::vector<std::uint64_t>> recompute_counts_of(std::span<const NodeId> nodes) const;
    Result<std::uint64_t> total_dependency_edges() const;
    Result<std::uint64_t> total_reverse_dependency_edges() const;
    Result<std::vector<NodeId>> isolated_nodes() const;
    Result<std::uint64_t> prune_dangling_dependencies();
    Result<std::uint64_t> relabel_by_prefix(std::string_view old_prefix, std::string_view new_prefix);
    Result<void> relabel_many(std::span<const NodeId> nodes, std::string_view new_prefix);
    Result<std::vector<std::pair<NodeId, NodeId>>> dependency_pairs() const;
    Result<std::vector<std::pair<NodeId, NodeId>>> reverse_dependency_pairs() const;
    Result<NodeId> max_recompute_count_node() const;
    Result<NodeId> min_recompute_count_node() const;
    Result<std::vector<NodeId>> nodes_with_min_recompute(std::uint64_t min_count) const;
    Result<std::vector<NodeId>> nodes_with_max_recompute(std::uint64_t max_count) const;
    Result<void> invalidate_by_kind(NodeKind kind);
    Result<void> recompute_by_kind(NodeKind kind);
    Result<void> remove_nodes_many(std::span<const NodeId> nodes);
    Result<std::uint64_t> clear_nodes_of_kind(NodeKind kind);
    Result<bool> contains_label_token(std::string_view token) const;
    Result<std::unordered_map<std::string, std::uint64_t>> label_histogram_prefix(std::uint64_t prefix_len) const;
    Result<std::vector<BodyId>> body_binding_bodies() const;
    Result<std::uint64_t> bound_body_count() const;
    Result<void> unbind_body(BodyId body_id);
    Result<void> unbind_all_bodies();
    Result<bool> has_any_invalid() const;
    Result<bool> has_any_dependency() const;
    Result<std::vector<NodeId>> invalid_nodes_of_kind(NodeKind kind) const;
    Result<std::vector<NodeId>> valid_nodes_of_kind(NodeKind kind) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

}  // namespace axiom
