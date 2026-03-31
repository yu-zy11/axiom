#pragma once

#include <string_view>

#include "axiom/core/types.h"
#include "axiom/internal/core/kernel_state.h"

namespace axiom::detail {

bool eval_has_node(const KernelState& state, NodeId node_id);
bool eval_dependency_exists(const KernelState& state, NodeId from, NodeId to);
bool eval_has_path(const KernelState& state, std::uint64_t from, std::uint64_t target);
void bind_eval_body_if_possible(KernelState& state, NodeId node_id, std::string_view label);

}  // namespace axiom::detail
