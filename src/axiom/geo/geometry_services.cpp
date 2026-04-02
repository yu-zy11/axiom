#include "axiom/geo/geometry_services.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <sstream>

#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/geo/geo_service_internal.h"
#include "axiom/internal/math/math_internal_utils.h"
#include "axiom/internal/geo/geometry_detail_bezier.h"

namespace axiom {
namespace geo_internal {

#include "axiom/internal/geo/geo_service_internal_part1.inc"
#include "axiom/internal/geo/geo_service_internal_part2.inc"

}  // namespace geo_internal

using namespace geo_internal;

#include "axiom/internal/geo/geometry_services_factories.inc"
#include "axiom/internal/geo/geometry_services_curve.inc"
#include "axiom/internal/geo/geometry_services_surface_eval.inc"
#include "axiom/internal/geo/geometry_services_surface_closest.inc"
#include "axiom/internal/geo/geometry_services_surface_domain_bbox.inc"
#include "axiom/internal/geo/geometry_services_transform_intersect.inc"

}  // namespace axiom
