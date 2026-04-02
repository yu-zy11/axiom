#include "axiom/io/io_service.h"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <regex>
#include <sstream>
#include <type_traits>
#include <system_error>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <iomanip>

#include "axiom/heal/heal_services.h"
#include "axiom/rep/representation_conversion_service.h"
#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/rep/representation_internal_utils.h"

#include "axiom/internal/io/io_service_internal.h"
#include "axiom/internal/io/step_iges_standard_scan.h"

namespace axiom {

using namespace io_internal;

IOService::IOService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

#include "axiom/internal/io/io_service_part1.inc"
#include "axiom/internal/io/io_service_part2.inc"
#include "axiom/internal/io/io_service_part3.inc"

}  // namespace axiom
