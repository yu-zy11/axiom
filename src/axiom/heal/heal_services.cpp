#include "axiom/heal/heal_services.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "axiom/geo/geometry_services.h"
#include "axiom/rep/representation_conversion_service.h"
#include "axiom/topo/topology_service.h"
#include "axiom/internal/heal/mesh_self_intersection.h"
#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/eval_graph_invalidation.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/core/topology_materialization.h"
#include "axiom/internal/math/math_internal_utils.h"

namespace axiom {

namespace {

#include "axiom/internal/heal/heal_helpers_a.inc"
#include "axiom/internal/heal/heal_helpers_b.inc"

}  // namespace

#include "axiom/internal/heal/heal_validation.inc"
#include "axiom/internal/heal/heal_repair.inc"

}  // namespace axiom
