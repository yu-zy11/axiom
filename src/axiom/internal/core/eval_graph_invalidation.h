#pragma once

#include <initializer_list>
#include <span>
#include <unordered_set>

#include "axiom/internal/core/kernel_state.h"

namespace axiom::detail {

inline void invalidate_eval_downstream(KernelState& state, std::uint64_t node) {
    ++state.eval_invalidation_bridge.downstream_invalidation_steps;
    state.eval_invalid[node] = true;
    const auto rev_it = state.eval_reverse_dependencies.find(node);
    if (rev_it == state.eval_reverse_dependencies.end()) {
        return;
    }
    for (const auto dependent : rev_it->second) {
        if (!state.eval_invalid[dependent]) {
            invalidate_eval_downstream(state, dependent);
        }
    }
}

inline void invalidate_eval_for_body(KernelState& state, BodyId body_id) {
    ++state.eval_invalidation_bridge.for_body_entries;
    const auto it = state.eval_body_bindings.find(body_id.value);
    if (it == state.eval_body_bindings.end()) {
        return;
    }
    for (const auto node_value : it->second) {
        if (state.eval_nodes.find(node_value) == state.eval_nodes.end()) {
            continue;
        }
        invalidate_eval_downstream(state, node_value);
    }
}

inline void invalidate_eval_for_bodies(KernelState& state, std::initializer_list<BodyId> body_ids) {
    ++state.eval_invalidation_bridge.for_bodies_batches;
    state.eval_invalidation_bridge.for_bodies_list_size_total +=
        static_cast<std::uint64_t>(body_ids.size());
    std::unordered_set<std::uint64_t> dedup;
    for (const auto body_id : body_ids) {
        if (!dedup.insert(body_id.value).second) {
            continue;
        }
        invalidate_eval_for_body(state, body_id);
    }
}

inline void invalidate_eval_for_faces(KernelState& state, std::span<const FaceId> faces) {
    ++state.eval_invalidation_bridge.for_faces_entries;
    std::unordered_set<std::uint64_t> body_values;
    for (const auto face_id : faces) {
        const auto shells_it = state.face_to_shells.find(face_id.value);
        if (shells_it == state.face_to_shells.end()) {
            continue;
        }
        for (const auto shell_value : shells_it->second) {
            const auto bodies_it = state.shell_to_bodies.find(shell_value);
            if (bodies_it == state.shell_to_bodies.end()) {
                continue;
            }
            for (const auto body_value : bodies_it->second) {
                body_values.insert(body_value);
            }
        }
    }
    for (const auto body_value : body_values) {
        invalidate_eval_for_body(state, BodyId {body_value});
    }
}

}  // namespace axiom::detail

