#include "axiom/internal/eval/eval_internal_utils.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace axiom::detail {

bool eval_has_node(const KernelState& state, NodeId node_id) {
    return state.eval_nodes.find(node_id.value) != state.eval_nodes.end();
}

bool eval_dependency_exists(const KernelState& state, NodeId from, NodeId to) {
    const auto it = state.eval_dependencies.find(from.value);
    if (it == state.eval_dependencies.end()) {
        return false;
    }
    return std::find(it->second.begin(), it->second.end(), to.value) != it->second.end();
}

bool eval_has_path(const KernelState& state, std::uint64_t from, std::uint64_t target) {
    if (from == target) {
        return true;
    }
    std::unordered_set<std::uint64_t> visited;
    std::vector<std::uint64_t> stack {from};
    while (!stack.empty()) {
        const auto current = stack.back();
        stack.pop_back();
        if (current == target) {
            return true;
        }
        if (!visited.insert(current).second) {
            continue;
        }
        const auto it = state.eval_dependencies.find(current);
        if (it == state.eval_dependencies.end()) {
            continue;
        }
        for (const auto next : it->second) {
            if (!visited.contains(next)) {
                stack.push_back(next);
            }
        }
    }
    return false;
}

void bind_eval_body_if_possible(KernelState& state, NodeId node_id, std::string_view label) {
    constexpr std::string_view kPrefix = "body:";
    if (!label.starts_with(kPrefix)) {
        return;
    }

    try {
        const auto raw = std::stoull(std::string(label.substr(kPrefix.size())));
        if (state.bodies.find(raw) == state.bodies.end()) {
            return;
        }
        auto& bindings = state.eval_body_bindings[raw];
        if (std::find(bindings.begin(), bindings.end(), node_id.value) == bindings.end()) {
            bindings.push_back(node_id.value);
        }
    } catch (...) {
    }
}

}  // namespace axiom::detail
