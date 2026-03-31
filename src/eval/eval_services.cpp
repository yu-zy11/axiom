#include "axiom/eval/eval_services.h"

#include <functional>
#include <unordered_set>

#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/eval_graph_invalidation.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/eval/eval_internal_utils.h"

namespace axiom {

EvalGraphService::EvalGraphService(std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)) {}

Result<NodeId> EvalGraphService::register_node(NodeKind kind,
                                               std::string_view label) {
  const auto id = NodeId{state_->allocate_id()};
  state_->eval_nodes.emplace(id.value, kind);
  state_->eval_labels.emplace(id.value, std::string(label));
  state_->eval_invalid[id.value] = false;
  state_->eval_recompute_count[id.value] = 0;
  detail::bind_eval_body_if_possible(*state_, id, label);
  return ok_result(id, state_->create_diagnostic("已注册求值图节点"));
}

Result<void> EvalGraphService::add_dependency(NodeId from, NodeId to) {
  if (!detail::eval_has_node(*state_, from) || !detail::eval_has_node(*state_, to) ||
      from.value == to.value) {
    return detail::invalid_input_void(
        *state_, diag_codes::kCoreInvalidHandle,
        "求值图依赖添加失败：节点不存在或依赖关系非法", "求值图依赖添加失败");
  }
  if (detail::eval_dependency_exists(*state_, from, to)) {
    return ok_void(state_->create_diagnostic("求值图依赖已存在"));
  }
  if (detail::eval_has_path(*state_, to.value, from.value)) {
    return detail::failed_void(
        *state_, StatusCode::OperationFailed, diag_codes::kEvalCycleDetected,
        "求值图依赖添加失败：将产生循环依赖", "求值图依赖添加失败");
  }

  state_->eval_dependencies[from.value].push_back(to.value);
  state_->eval_reverse_dependencies[to.value].push_back(from.value);
  return ok_void(state_->create_diagnostic("已添加求值图依赖"));
}

Result<void> EvalGraphService::invalidate(NodeId node_id) {
  if (!detail::eval_has_node(*state_, node_id)) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "求值图失效失败：目标节点不存在",
                                      "求值图失效失败");
  }
  detail::invalidate_eval_downstream(*state_, node_id.value);
  return ok_void(state_->create_diagnostic("已标记节点失效"));
}

Result<void> EvalGraphService::invalidate_body(BodyId body_id) {
  if (!detail::has_body(*state_, body_id)) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "体相关节点失效失败：目标体不存在",
                                      "体相关节点失效失败");
  }

  detail::invalidate_eval_for_body(*state_, body_id);
  return ok_void(state_->create_diagnostic("已标记体相关节点失效"));
}

Result<void> EvalGraphService::recompute(NodeId node_id) {
  if (!detail::eval_has_node(*state_, node_id)) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "求值图重算失败：目标节点不存在",
                                      "求值图重算失败");
  }

  std::unordered_set<std::uint64_t> recomputed_nodes;
  std::function<Result<void>(std::uint64_t)> recompute_node =
      [&](std::uint64_t current) -> Result<void> {
    if (recomputed_nodes.contains(current)) {
      return ok_void(state_->create_diagnostic("已跳过重复重算节点"));
    }

    const auto dep_it = state_->eval_dependencies.find(current);
    if (dep_it != state_->eval_dependencies.end()) {
      for (const auto dependency : dep_it->second) {
        if (state_->eval_nodes.find(dependency) == state_->eval_nodes.end()) {
          return detail::failed_void(*state_, StatusCode::InternalError,
                                     diag_codes::kCoreInvalidHandle,
                                     "求值图重算失败：存在指向缺失节点的依赖",
                                     "求值图重算失败");
        }
        if (state_->eval_invalid[dependency]) {
          const auto dep_result = recompute_node(dependency);
          if (dep_result.status != StatusCode::Ok) {
            return dep_result;
          }
        }
      }
    }
    state_->eval_invalid[current] = false;
    state_->eval_recompute_count[current] += 1;
    recomputed_nodes.insert(current);
    return ok_void(state_->create_diagnostic("已完成单节点重算"));
  };

  const auto recompute_result = recompute_node(node_id.value);
  if (recompute_result.status != StatusCode::Ok) {
    return recompute_result;
  }
  return ok_void(state_->create_diagnostic("已完成节点重算"));
}

Result<bool> EvalGraphService::is_invalid(NodeId node_id) const {
  if (!detail::eval_has_node(*state_, node_id)) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kCoreInvalidHandle,
        "求值图状态查询失败：目标节点不存在", "求值图状态查询失败");
  }
  return ok_result(state_->eval_invalid[node_id.value],
                   state_->create_diagnostic("已查询节点失效状态"));
}

Result<std::uint64_t> EvalGraphService::recompute_count(NodeId node_id) const {
  if (!detail::eval_has_node(*state_, node_id)) {
    return detail::invalid_input_result<std::uint64_t>(
        *state_, diag_codes::kCoreInvalidHandle,
        "求值图重算计数查询失败：目标节点不存在", "求值图重算计数查询失败");
  }
  return ok_result(state_->eval_recompute_count[node_id.value],
                   state_->create_diagnostic("已查询节点重算计数"));
}

Result<std::vector<NodeId>> EvalGraphService::dependencies_of(NodeId node_id) const {
  if (!detail::eval_has_node(*state_, node_id)) {
    return detail::invalid_input_result<std::vector<NodeId>>(
        *state_, diag_codes::kCoreInvalidHandle,
        "求值图依赖查询失败：目标节点不存在", "求值图依赖查询失败");
  }
  std::vector<NodeId> out;
  const auto it = state_->eval_dependencies.find(node_id.value);
  if (it != state_->eval_dependencies.end()) {
    out.reserve(it->second.size());
    for (const auto dep : it->second) {
      out.push_back(NodeId{dep});
    }
  }
  return ok_result(std::move(out), state_->create_diagnostic("已查询节点依赖列表"));
}

Result<std::vector<NodeId>> EvalGraphService::dependents_of(NodeId node_id) const {
  if (!detail::eval_has_node(*state_, node_id)) {
    return detail::invalid_input_result<std::vector<NodeId>>(
        *state_, diag_codes::kCoreInvalidHandle,
        "求值图反向依赖查询失败：目标节点不存在", "求值图反向依赖查询失败");
  }
  std::vector<NodeId> out;
  const auto it = state_->eval_reverse_dependencies.find(node_id.value);
  if (it != state_->eval_reverse_dependencies.end()) {
    out.reserve(it->second.size());
    for (const auto dep : it->second) {
      out.push_back(NodeId{dep});
    }
  }
  return ok_result(std::move(out), state_->create_diagnostic("已查询节点反向依赖列表"));
}

Result<bool> EvalGraphService::exists(NodeId node_id) const {
  return ok_result(detail::eval_has_node(*state_, node_id),
                   state_->create_diagnostic("已查询节点存在性"));
}

Result<NodeKind> EvalGraphService::kind_of(NodeId node_id) const {
  const auto it = state_->eval_nodes.find(node_id.value);
  if (it == state_->eval_nodes.end()) {
    return detail::invalid_input_result<NodeKind>(
        *state_, diag_codes::kCoreInvalidHandle, "求值图节点类型查询失败：目标节点不存在",
        "求值图节点类型查询失败");
  }
  return ok_result(it->second, state_->create_diagnostic("已查询节点类型"));
}

Result<std::string> EvalGraphService::label_of(NodeId node_id) const {
  if (!detail::eval_has_node(*state_, node_id)) {
    return detail::invalid_input_result<std::string>(
        *state_, diag_codes::kCoreInvalidHandle, "求值图节点标签查询失败：目标节点不存在",
        "求值图节点标签查询失败");
  }
  return ok_result(state_->eval_labels[node_id.value],
                   state_->create_diagnostic("已查询节点标签"));
}

Result<void> EvalGraphService::set_label(NodeId node_id, std::string_view label) {
  if (!detail::eval_has_node(*state_, node_id)) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "求值图节点标签设置失败：目标节点不存在",
                                      "求值图节点标签设置失败");
  }
  state_->eval_labels[node_id.value] = std::string(label);
  detail::bind_eval_body_if_possible(*state_, node_id, label);
  return ok_void(state_->create_diagnostic("已设置节点标签"));
}

Result<std::uint64_t> EvalGraphService::node_count() const {
  return ok_result<std::uint64_t>(static_cast<std::uint64_t>(state_->eval_nodes.size()),
                                  state_->create_diagnostic("已查询节点总数"));
}

Result<std::uint64_t> EvalGraphService::invalid_node_count() const {
  std::uint64_t n = 0;
  for (const auto& [id, invalid] : state_->eval_invalid) {
    (void)id;
    if (invalid) ++n;
  }
  return ok_result<std::uint64_t>(n, state_->create_diagnostic("已查询失效节点数量"));
}

Result<std::uint64_t> EvalGraphService::valid_node_count() const {
  const auto total = node_count();
  const auto invalid = invalid_node_count();
  if (total.status != StatusCode::Ok || invalid.status != StatusCode::Ok ||
      !total.value.has_value() || !invalid.value.has_value()) {
    return detail::failed_result<std::uint64_t>(
        *state_, StatusCode::InternalError, diag_codes::kCoreInvalidHandle,
        "求值图有效节点数量查询失败", "求值图有效节点数量查询失败");
  }
  return ok_result<std::uint64_t>(*total.value - *invalid.value,
                                  state_->create_diagnostic("已查询有效节点数量"));
}

Result<std::uint64_t> EvalGraphService::dependency_count(NodeId node_id) const {
  const auto deps = dependencies_of(node_id);
  if (deps.status != StatusCode::Ok || !deps.value.has_value()) {
    return error_result<std::uint64_t>(deps.status, deps.diagnostic_id);
  }
  return ok_result<std::uint64_t>(static_cast<std::uint64_t>(deps.value->size()),
                                  state_->create_diagnostic("已查询依赖数量"));
}

Result<std::uint64_t> EvalGraphService::dependent_count(NodeId node_id) const {
  const auto deps = dependents_of(node_id);
  if (deps.status != StatusCode::Ok || !deps.value.has_value()) {
    return error_result<std::uint64_t>(deps.status, deps.diagnostic_id);
  }
  return ok_result<std::uint64_t>(static_cast<std::uint64_t>(deps.value->size()),
                                  state_->create_diagnostic("已查询反向依赖数量"));
}

Result<bool> EvalGraphService::has_dependency(NodeId from, NodeId to) const {
  if (!detail::eval_has_node(*state_, from) || !detail::eval_has_node(*state_, to)) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kCoreInvalidHandle, "求值图依赖查询失败：节点不存在",
        "求值图依赖查询失败");
  }
  return ok_result(detail::eval_dependency_exists(*state_, from, to),
                   state_->create_diagnostic("已查询依赖是否存在"));
}

Result<void> EvalGraphService::remove_dependency(NodeId from, NodeId to) {
  if (!detail::eval_has_node(*state_, from) || !detail::eval_has_node(*state_, to)) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "求值图依赖移除失败：节点不存在", "求值图依赖移除失败");
  }
  auto& deps = state_->eval_dependencies[from.value];
  deps.erase(std::remove(deps.begin(), deps.end(), to.value), deps.end());
  auto& rev = state_->eval_reverse_dependencies[to.value];
  rev.erase(std::remove(rev.begin(), rev.end(), from.value), rev.end());
  return ok_void(state_->create_diagnostic("已移除依赖"));
}

Result<void> EvalGraphService::clear_dependencies(NodeId node_id) {
  if (!detail::eval_has_node(*state_, node_id)) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "求值图依赖清空失败：节点不存在", "求值图依赖清空失败");
  }
  const auto deps = state_->eval_dependencies[node_id.value];
  for (const auto dep : deps) {
    auto& rev = state_->eval_reverse_dependencies[dep];
    rev.erase(std::remove(rev.begin(), rev.end(), node_id.value), rev.end());
  }
  state_->eval_dependencies[node_id.value].clear();
  return ok_void(state_->create_diagnostic("已清空节点依赖"));
}

Result<void> EvalGraphService::clear_dependents(NodeId node_id) {
  if (!detail::eval_has_node(*state_, node_id)) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "求值图反向依赖清空失败：节点不存在",
                                      "求值图反向依赖清空失败");
  }
  const auto rev = state_->eval_reverse_dependencies[node_id.value];
  for (const auto parent : rev) {
    auto& deps = state_->eval_dependencies[parent];
    deps.erase(std::remove(deps.begin(), deps.end(), node_id.value), deps.end());
  }
  state_->eval_reverse_dependencies[node_id.value].clear();
  return ok_void(state_->create_diagnostic("已清空节点反向依赖"));
}

Result<void> EvalGraphService::reset_recompute_count(NodeId node_id) {
  if (!detail::eval_has_node(*state_, node_id)) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "重算计数重置失败：节点不存在", "重算计数重置失败");
  }
  state_->eval_recompute_count[node_id.value] = 0;
  return ok_void(state_->create_diagnostic("已重置节点重算计数"));
}

Result<void> EvalGraphService::reset_all_recompute_counts() {
  for (auto& [id, count] : state_->eval_recompute_count) {
    (void)id;
    count = 0;
  }
  return ok_void(state_->create_diagnostic("已重置全部重算计数"));
}

Result<std::uint64_t> EvalGraphService::total_recompute_count() const {
  std::uint64_t total = 0;
  for (const auto& [id, count] : state_->eval_recompute_count) {
    (void)id;
    total += count;
  }
  return ok_result<std::uint64_t>(total, state_->create_diagnostic("已统计总重算计数"));
}

Result<std::vector<NodeId>> EvalGraphService::all_nodes() const {
  std::vector<NodeId> out;
  out.reserve(state_->eval_nodes.size());
  for (const auto& [id, kind] : state_->eval_nodes) {
    (void)kind;
    out.push_back(NodeId{id});
  }
  return ok_result(std::move(out), state_->create_diagnostic("已查询全部节点"));
}

Result<std::vector<NodeId>> EvalGraphService::invalid_nodes() const {
  std::vector<NodeId> out;
  for (const auto& [id, invalid] : state_->eval_invalid) {
    if (invalid) out.push_back(NodeId{id});
  }
  return ok_result(std::move(out), state_->create_diagnostic("已查询失效节点列表"));
}

Result<std::vector<NodeId>> EvalGraphService::valid_nodes() const {
  std::vector<NodeId> out;
  for (const auto& [id, invalid] : state_->eval_invalid) {
    if (!invalid) out.push_back(NodeId{id});
  }
  return ok_result(std::move(out), state_->create_diagnostic("已查询有效节点列表"));
}

Result<std::vector<NodeId>> EvalGraphService::nodes_of_kind(NodeKind kind) const {
  std::vector<NodeId> out;
  for (const auto& [id, k] : state_->eval_nodes) {
    if (k == kind) out.push_back(NodeId{id});
  }
  return ok_result(std::move(out), state_->create_diagnostic("已按类型查询节点"));
}

Result<std::vector<std::string>>
EvalGraphService::labels_of_nodes(std::span<const NodeId> nodes) const {
  std::vector<std::string> out;
  out.reserve(nodes.size());
  for (const auto node : nodes) {
    if (!detail::eval_has_node(*state_, node)) {
      return detail::invalid_input_result<std::vector<std::string>>(
          *state_, diag_codes::kCoreInvalidHandle, "节点标签批量查询失败：存在无效节点",
          "节点标签批量查询失败");
    }
    out.push_back(state_->eval_labels.at(node.value));
  }
  return ok_result(std::move(out), state_->create_diagnostic("已批量查询节点标签"));
}

Result<std::vector<NodeId>> EvalGraphService::find_by_label_token(
    std::string_view token, std::uint64_t max_results) const {
  if (token.empty() || max_results == 0) {
    return detail::invalid_input_result<std::vector<NodeId>>(
        *state_, diag_codes::kCoreParameterOutOfRange, "节点标签检索失败：参数非法",
        "节点标签检索失败");
  }
  std::vector<NodeId> out;
  for (const auto& [id, label] : state_->eval_labels) {
    if (out.size() >= max_results) break;
    if (label.find(token) != std::string::npos) out.push_back(NodeId{id});
  }
  return ok_result(std::move(out), state_->create_diagnostic("已按标签检索节点"));
}

Result<void> EvalGraphService::invalidate_many(std::span<const NodeId> nodes) {
  if (nodes.empty()) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreParameterOutOfRange,
                                      "批量失效失败：节点列表为空", "批量失效失败");
  }
  for (const auto node : nodes) {
    const auto r = invalidate(node);
    if (r.status != StatusCode::Ok) return r;
  }
  return ok_void(state_->create_diagnostic("已批量失效节点"));
}

Result<void> EvalGraphService::recompute_many(std::span<const NodeId> nodes) {
  if (nodes.empty()) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreParameterOutOfRange,
                                      "批量重算失败：节点列表为空", "批量重算失败");
  }
  for (const auto node : nodes) {
    const auto r = recompute(node);
    if (r.status != StatusCode::Ok) return r;
  }
  return ok_void(state_->create_diagnostic("已批量重算节点"));
}

Result<void> EvalGraphService::remove_node(NodeId node_id) {
  if (!detail::eval_has_node(*state_, node_id)) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "节点移除失败：节点不存在", "节点移除失败");
  }
  clear_dependencies(node_id);
  clear_dependents(node_id);
  state_->eval_nodes.erase(node_id.value);
  state_->eval_labels.erase(node_id.value);
  state_->eval_invalid.erase(node_id.value);
  state_->eval_recompute_count.erase(node_id.value);
  for (auto& [body, nodes] : state_->eval_body_bindings) {
    (void)body;
    nodes.erase(std::remove(nodes.begin(), nodes.end(), node_id.value), nodes.end());
  }
  return ok_void(state_->create_diagnostic("已移除节点"));
}

Result<void> EvalGraphService::clear_graph() {
  state_->eval_nodes.clear();
  state_->eval_labels.clear();
  state_->eval_invalid.clear();
  state_->eval_recompute_count.clear();
  state_->eval_dependencies.clear();
  state_->eval_reverse_dependencies.clear();
  state_->eval_body_bindings.clear();
  return ok_void(state_->create_diagnostic("已清空求值图"));
}

Result<std::uint64_t> EvalGraphService::body_binding_count(BodyId body_id) const {
  if (!detail::has_body(*state_, body_id)) {
    return detail::invalid_input_result<std::uint64_t>(
        *state_, diag_codes::kCoreInvalidHandle, "体绑定计数查询失败：体不存在",
        "体绑定计数查询失败");
  }
  const auto it = state_->eval_body_bindings.find(body_id.value);
  return ok_result<std::uint64_t>(it == state_->eval_body_bindings.end() ? 0 : static_cast<std::uint64_t>(it->second.size()),
                                  state_->create_diagnostic("已查询体绑定节点数量"));
}

Result<std::vector<NodeId>> EvalGraphService::nodes_of_body(BodyId body_id) const {
  if (!detail::has_body(*state_, body_id)) {
    return detail::invalid_input_result<std::vector<NodeId>>(
        *state_, diag_codes::kCoreInvalidHandle, "体绑定节点查询失败：体不存在",
        "体绑定节点查询失败");
  }
  std::vector<NodeId> out;
  const auto it = state_->eval_body_bindings.find(body_id.value);
  if (it != state_->eval_body_bindings.end()) {
    for (const auto id : it->second) out.push_back(NodeId{id});
  }
  return ok_result(std::move(out), state_->create_diagnostic("已查询体绑定节点"));
}

Result<bool> EvalGraphService::is_leaf(NodeId node_id) const {
  const auto deps = dependency_count(node_id);
  if (deps.status != StatusCode::Ok || !deps.value.has_value()) return error_result<bool>(deps.status, deps.diagnostic_id);
  return ok_result(*deps.value == 0, state_->create_diagnostic("已查询是否叶子节点"));
}

Result<bool> EvalGraphService::is_root(NodeId node_id) const {
  const auto deps = dependent_count(node_id);
  if (deps.status != StatusCode::Ok || !deps.value.has_value()) return error_result<bool>(deps.status, deps.diagnostic_id);
  return ok_result(*deps.value == 0, state_->create_diagnostic("已查询是否根节点"));
}

Result<std::vector<NodeId>> EvalGraphService::ids_sorted_asc() const {
  auto nodes = all_nodes();
  if (nodes.status != StatusCode::Ok || !nodes.value.has_value()) return error_result<std::vector<NodeId>>(nodes.status, nodes.diagnostic_id);
  std::sort(nodes.value->begin(), nodes.value->end(), [](NodeId a, NodeId b){ return a.value < b.value; });
  return ok_result(std::move(*nodes.value), state_->create_diagnostic("已升序查询节点ID"));
}

Result<std::vector<NodeId>> EvalGraphService::ids_sorted_desc() const {
  auto nodes = all_nodes();
  if (nodes.status != StatusCode::Ok || !nodes.value.has_value()) return error_result<std::vector<NodeId>>(nodes.status, nodes.diagnostic_id);
  std::sort(nodes.value->begin(), nodes.value->end(), [](NodeId a, NodeId b){ return a.value > b.value; });
  return ok_result(std::move(*nodes.value), state_->create_diagnostic("已降序查询节点ID"));
}

Result<double> EvalGraphService::invalid_ratio() const {
  const auto total = node_count();
  const auto invalid = invalid_node_count();
  if (total.status != StatusCode::Ok || invalid.status != StatusCode::Ok || !total.value.has_value() || !invalid.value.has_value()) return detail::failed_result<double>(*state_, StatusCode::InternalError, diag_codes::kCoreInvalidHandle, "失效占比查询失败", "失效占比查询失败");
  if (*total.value == 0) return ok_result(0.0, state_->create_diagnostic("已查询失效占比"));
  return ok_result(static_cast<double>(*invalid.value) / static_cast<double>(*total.value), state_->create_diagnostic("已查询失效占比"));
}

Result<std::vector<std::uint64_t>> EvalGraphService::recompute_counts_of(std::span<const NodeId> nodes) const {
  std::vector<std::uint64_t> out;
  out.reserve(nodes.size());
  for (const auto node : nodes) {
    const auto c = recompute_count(node);
    if (c.status != StatusCode::Ok || !c.value.has_value()) return error_result<std::vector<std::uint64_t>>(c.status, c.diagnostic_id);
    out.push_back(*c.value);
  }
  return ok_result(std::move(out), state_->create_diagnostic("已批量查询重算计数"));
}

Result<std::uint64_t> EvalGraphService::total_dependency_edges() const {
  std::uint64_t total = 0;
  for (const auto& [id, deps] : state_->eval_dependencies) { (void)id; total += static_cast<std::uint64_t>(deps.size()); }
  return ok_result<std::uint64_t>(total, state_->create_diagnostic("已统计依赖边总数"));
}

Result<std::uint64_t> EvalGraphService::total_reverse_dependency_edges() const {
  std::uint64_t total = 0;
  for (const auto& [id, deps] : state_->eval_reverse_dependencies) { (void)id; total += static_cast<std::uint64_t>(deps.size()); }
  return ok_result<std::uint64_t>(total, state_->create_diagnostic("已统计反向依赖边总数"));
}

Result<std::vector<NodeId>> EvalGraphService::isolated_nodes() const {
  std::vector<NodeId> out;
  for (const auto& [id, kind] : state_->eval_nodes) {
    (void)kind;
    const auto dep = state_->eval_dependencies.find(id);
    const auto rev = state_->eval_reverse_dependencies.find(id);
    const auto dep_empty = dep == state_->eval_dependencies.end() || dep->second.empty();
    const auto rev_empty = rev == state_->eval_reverse_dependencies.end() || rev->second.empty();
    if (dep_empty && rev_empty) out.push_back(NodeId{id});
  }
  return ok_result(std::move(out), state_->create_diagnostic("已查询孤立节点"));
}

Result<std::uint64_t> EvalGraphService::prune_dangling_dependencies() {
  std::uint64_t removed = 0;
  for (auto& [id, deps] : state_->eval_dependencies) {
    (void)id;
    const auto before = deps.size();
    deps.erase(std::remove_if(deps.begin(), deps.end(), [this](std::uint64_t dep){ return state_->eval_nodes.find(dep) == state_->eval_nodes.end(); }), deps.end());
    removed += static_cast<std::uint64_t>(before - deps.size());
  }
  for (auto& [id, deps] : state_->eval_reverse_dependencies) {
    (void)id;
    deps.erase(std::remove_if(deps.begin(), deps.end(), [this](std::uint64_t dep){ return state_->eval_nodes.find(dep) == state_->eval_nodes.end(); }), deps.end());
  }
  return ok_result<std::uint64_t>(removed, state_->create_diagnostic("已清理悬空依赖"));
}

Result<std::uint64_t> EvalGraphService::relabel_by_prefix(std::string_view old_prefix, std::string_view new_prefix) {
  if (old_prefix.empty()) return detail::invalid_input_result<std::uint64_t>(*state_, diag_codes::kCoreParameterOutOfRange, "批量重命名失败：旧前缀为空", "批量重命名失败");
  std::uint64_t n = 0;
  for (auto& [id, label] : state_->eval_labels) {
    if (label.rfind(std::string(old_prefix), 0) == 0) {
      label = std::string(new_prefix) + label.substr(old_prefix.size());
      ++n;
      detail::bind_eval_body_if_possible(*state_, NodeId{id}, label);
    }
  }
  return ok_result<std::uint64_t>(n, state_->create_diagnostic("已按前缀重命名节点"));
}

Result<void> EvalGraphService::relabel_many(std::span<const NodeId> nodes, std::string_view new_prefix) {
  for (const auto node : nodes) {
    if (!detail::eval_has_node(*state_, node)) return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle, "批量重命名失败：存在无效节点", "批量重命名失败");
    state_->eval_labels[node.value] = std::string(new_prefix) + std::to_string(node.value);
    detail::bind_eval_body_if_possible(*state_, node, state_->eval_labels[node.value]);
  }
  return ok_void(state_->create_diagnostic("已批量重命名节点"));
}

Result<std::vector<std::pair<NodeId, NodeId>>> EvalGraphService::dependency_pairs() const {
  std::vector<std::pair<NodeId, NodeId>> out;
  for (const auto& [from, deps] : state_->eval_dependencies) for (const auto dep : deps) out.emplace_back(NodeId{from}, NodeId{dep});
  return ok_result(std::move(out), state_->create_diagnostic("已查询依赖边对"));
}

Result<std::vector<std::pair<NodeId, NodeId>>> EvalGraphService::reverse_dependency_pairs() const {
  std::vector<std::pair<NodeId, NodeId>> out;
  for (const auto& [to, deps] : state_->eval_reverse_dependencies) for (const auto dep : deps) out.emplace_back(NodeId{to}, NodeId{dep});
  return ok_result(std::move(out), state_->create_diagnostic("已查询反向依赖边对"));
}

Result<NodeId> EvalGraphService::max_recompute_count_node() const {
  if (state_->eval_recompute_count.empty()) return ok_result(NodeId{0}, state_->create_diagnostic("已查询最大重算节点"));
  auto it = std::max_element(state_->eval_recompute_count.begin(), state_->eval_recompute_count.end(), [](const auto& a, const auto& b){ return a.second < b.second; });
  return ok_result(NodeId{it->first}, state_->create_diagnostic("已查询最大重算节点"));
}

Result<NodeId> EvalGraphService::min_recompute_count_node() const {
  if (state_->eval_recompute_count.empty()) return ok_result(NodeId{0}, state_->create_diagnostic("已查询最小重算节点"));
  auto it = std::min_element(state_->eval_recompute_count.begin(), state_->eval_recompute_count.end(), [](const auto& a, const auto& b){ return a.second < b.second; });
  return ok_result(NodeId{it->first}, state_->create_diagnostic("已查询最小重算节点"));
}

Result<std::vector<NodeId>> EvalGraphService::nodes_with_min_recompute(std::uint64_t min_count) const {
  std::vector<NodeId> out;
  for (const auto& [id, count] : state_->eval_recompute_count) if (count >= min_count) out.push_back(NodeId{id});
  return ok_result(std::move(out), state_->create_diagnostic("已按最小重算次数筛选节点"));
}

Result<std::vector<NodeId>> EvalGraphService::nodes_with_max_recompute(std::uint64_t max_count) const {
  std::vector<NodeId> out;
  for (const auto& [id, count] : state_->eval_recompute_count) if (count <= max_count) out.push_back(NodeId{id});
  return ok_result(std::move(out), state_->create_diagnostic("已按最大重算次数筛选节点"));
}

Result<void> EvalGraphService::invalidate_by_kind(NodeKind kind) {
  for (const auto& [id, k] : state_->eval_nodes) if (k == kind) state_->eval_invalid[id] = true;
  return ok_void(state_->create_diagnostic("已按类型失效节点"));
}

Result<void> EvalGraphService::recompute_by_kind(NodeKind kind) {
  for (const auto& [id, k] : state_->eval_nodes) {
    if (k == kind && state_->eval_invalid[id]) {
      const auto r = recompute(NodeId{id});
      if (r.status != StatusCode::Ok) return r;
    }
  }
  return ok_void(state_->create_diagnostic("已按类型重算节点"));
}

Result<void> EvalGraphService::remove_nodes_many(std::span<const NodeId> nodes) {
  for (const auto node : nodes) {
    const auto r = remove_node(node);
    if (r.status != StatusCode::Ok) return r;
  }
  return ok_void(state_->create_diagnostic("已批量移除节点"));
}

Result<std::uint64_t> EvalGraphService::clear_nodes_of_kind(NodeKind kind) {
  std::vector<NodeId> targets;
  for (const auto& [id, k] : state_->eval_nodes) if (k == kind) targets.push_back(NodeId{id});
  const auto n = static_cast<std::uint64_t>(targets.size());
  const auto r = remove_nodes_many(std::span<const NodeId>(targets));
  if (r.status != StatusCode::Ok) return error_result<std::uint64_t>(r.status, r.diagnostic_id);
  return ok_result<std::uint64_t>(n, state_->create_diagnostic("已按类型清空节点"));
}

Result<bool> EvalGraphService::contains_label_token(std::string_view token) const {
  if (token.empty()) return detail::invalid_input_result<bool>(*state_, diag_codes::kCoreParameterOutOfRange, "标签检索失败：关键字为空", "标签检索失败");
  for (const auto& [id, label] : state_->eval_labels) { (void)id; if (label.find(token) != std::string::npos) return ok_result(true, state_->create_diagnostic("已查询标签关键字")); }
  return ok_result(false, state_->create_diagnostic("已查询标签关键字"));
}

Result<std::unordered_map<std::string, std::uint64_t>> EvalGraphService::label_histogram_prefix(std::uint64_t prefix_len) const {
  if (prefix_len == 0) return detail::invalid_input_result<std::unordered_map<std::string, std::uint64_t>>(*state_, diag_codes::kCoreParameterOutOfRange, "标签直方图失败：前缀长度为0", "标签直方图失败");
  std::unordered_map<std::string, std::uint64_t> out;
  for (const auto& [id, label] : state_->eval_labels) {
    (void)id;
    const auto key = label.size() <= prefix_len ? label : label.substr(0, static_cast<std::size_t>(prefix_len));
    ++out[key];
  }
  return ok_result(std::move(out), state_->create_diagnostic("已统计标签前缀直方图"));
}

Result<std::vector<BodyId>> EvalGraphService::body_binding_bodies() const {
  std::vector<BodyId> out;
  out.reserve(state_->eval_body_bindings.size());
  for (const auto& [id, nodes] : state_->eval_body_bindings) { (void)nodes; out.push_back(BodyId{id}); }
  return ok_result(std::move(out), state_->create_diagnostic("已查询绑定体列表"));
}

Result<std::uint64_t> EvalGraphService::bound_body_count() const {
  return ok_result<std::uint64_t>(static_cast<std::uint64_t>(state_->eval_body_bindings.size()), state_->create_diagnostic("已统计绑定体数量"));
}

Result<void> EvalGraphService::unbind_body(BodyId body_id) {
  state_->eval_body_bindings.erase(body_id.value);
  return ok_void(state_->create_diagnostic("已解绑体与节点"));
}

Result<void> EvalGraphService::unbind_all_bodies() {
  state_->eval_body_bindings.clear();
  return ok_void(state_->create_diagnostic("已解绑全部体与节点"));
}

Result<bool> EvalGraphService::has_any_invalid() const {
  for (const auto& [id, invalid] : state_->eval_invalid) { (void)id; if (invalid) return ok_result(true, state_->create_diagnostic("已查询是否存在失效节点")); }
  return ok_result(false, state_->create_diagnostic("已查询是否存在失效节点"));
}

Result<bool> EvalGraphService::has_any_dependency() const {
  for (const auto& [id, deps] : state_->eval_dependencies) { (void)id; if (!deps.empty()) return ok_result(true, state_->create_diagnostic("已查询是否存在依赖边")); }
  return ok_result(false, state_->create_diagnostic("已查询是否存在依赖边"));
}

Result<std::vector<NodeId>> EvalGraphService::invalid_nodes_of_kind(NodeKind kind) const {
  std::vector<NodeId> out;
  for (const auto& [id, k] : state_->eval_nodes) if (k == kind && state_->eval_invalid.at(id)) out.push_back(NodeId{id});
  return ok_result(std::move(out), state_->create_diagnostic("已查询指定类型失效节点"));
}

Result<std::vector<NodeId>> EvalGraphService::valid_nodes_of_kind(NodeKind kind) const {
  std::vector<NodeId> out;
  for (const auto& [id, k] : state_->eval_nodes) if (k == kind && !state_->eval_invalid.at(id)) out.push_back(NodeId{id});
  return ok_result(std::move(out), state_->create_diagnostic("已查询指定类型有效节点"));
}

} // namespace axiom
