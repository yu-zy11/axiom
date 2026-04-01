#include "axiom/topo/topology_service.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "axiom/geo/geometry_services.h"
#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/math/math_internal_utils.h"
#include "axiom/internal/topo/topo_service_internal.h"

namespace axiom {

using namespace topo_internal;

namespace detail {

struct TopologyTransactionState {
  std::unordered_map<std::uint64_t, FaceRecord> original_faces;
  std::unordered_map<std::uint64_t, ShellRecord> original_shells;
  std::unordered_map<std::uint64_t, BodyRecord> original_bodies;

  void snapshot_face(const KernelState &state, FaceId face_id) {
    if (original_faces.find(face_id.value) != original_faces.end()) {
      return;
    }
    const auto it = state.faces.find(face_id.value);
    if (it != state.faces.end()) {
      original_faces.emplace(face_id.value, it->second);
    }
  }

  void snapshot_shell(const KernelState &state, ShellId shell_id) {
    if (original_shells.find(shell_id.value) != original_shells.end()) {
      return;
    }
    const auto it = state.shells.find(shell_id.value);
    if (it != state.shells.end()) {
      original_shells.emplace(shell_id.value, it->second);
    }
  }

  void snapshot_body(const KernelState &state, BodyId body_id) {
    if (original_bodies.find(body_id.value) != original_bodies.end()) {
      return;
    }
    const auto it = state.bodies.find(body_id.value);
    if (it != state.bodies.end()) {
      original_bodies.emplace(body_id.value, it->second);
    }
  }
};

}  // namespace detail

#include "axiom/internal/topo/topology_query.inc"
#include "axiom/internal/topo/topology_transaction.inc"
#include "axiom/internal/topo/topology_validation_a.inc"
#include "axiom/internal/topo/topology_validation_b.inc"
#include "axiom/internal/topo/topology_validation_c.inc"

TopologyService::TopologyService(std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)), query_service_(state_),
      validation_service_(state_) {}

TopologyTransaction TopologyService::begin_transaction() {
  return TopologyTransaction{state_};
}

TopologyQueryService &TopologyService::query() { return query_service_; }

TopologyValidationService &TopologyService::validate() {
  return validation_service_;
}

}  // namespace axiom
