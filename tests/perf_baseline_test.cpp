#include <chrono>
#include <cstdlib>
#include <iostream>

#include "axiom/sdk/kernel.h"

namespace {

std::uint64_t max_duration_ms() {
    constexpr std::uint64_t kDefaultMs = 4000;
    const char* env = std::getenv("AXM_PERF_MAX_MS");
    if (env == nullptr) {
        return kDefaultMs;
    }
    try {
        const auto value = std::stoull(env);
        return value == 0 ? kDefaultMs : value;
    } catch (...) {
        return kDefaultMs;
    }
}

std::uint64_t iteration_count() {
    constexpr std::uint64_t kDefaultIterations = 150;
    const char* env = std::getenv("AXM_PERF_ITERATIONS");
    if (env == nullptr) {
        return kDefaultIterations;
    }
    try {
        const auto value = std::stoull(env);
        return value == 0 ? kDefaultIterations : value;
    } catch (...) {
        return kDefaultIterations;
    }
}

}  // namespace

int main() {
    axiom::Kernel kernel;
    const auto start = std::chrono::steady_clock::now();

    for (std::uint64_t i = 0; i < iteration_count(); ++i) {
        const double t = static_cast<double>(i);
        auto box = kernel.primitives().box({t, t * 0.1, t * 0.2}, 10.0, 8.0, 6.0);
        auto cutter = kernel.primitives().cylinder({t + 2.0, t * 0.1 + 2.0, t * 0.2 + 3.0}, {0.0, 0.0, 1.0}, 1.0, 6.0);
        if (box.status != axiom::StatusCode::Ok || cutter.status != axiom::StatusCode::Ok ||
            !box.value.has_value() || !cutter.value.has_value()) {
            std::cerr << "perf baseline setup failed\n";
            return 1;
        }
        auto boolean_result = kernel.booleans().run(axiom::BooleanOp::Subtract, *box.value, *cutter.value, {});
        if (boolean_result.status != axiom::StatusCode::Ok && boolean_result.status != axiom::StatusCode::OperationFailed) {
            std::cerr << "perf baseline boolean status unexpected\n";
            return 1;
        }
        auto query_target = boolean_result.status == axiom::StatusCode::Ok && boolean_result.value.has_value()
                                ? boolean_result.value->output
                                : *box.value;
        auto props = kernel.query().mass_properties(query_target);
        if (props.status != axiom::StatusCode::Ok) {
            std::cerr << "perf baseline mass properties failed\n";
            return 1;
        }
    }

    const auto end = std::chrono::steady_clock::now();
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    if (elapsed_ms > static_cast<long long>(max_duration_ms())) {
        std::cerr << "perf baseline exceeded threshold, elapsed_ms=" << elapsed_ms << "\n";
        return 1;
    }
    return 0;
}
