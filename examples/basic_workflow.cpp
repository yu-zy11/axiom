#include <iostream>

#include "axiom/sdk/kernel.h"

int main() {
    axiom::Kernel kernel;

    auto box = kernel.primitives().box({0.0, 0.0, 0.0}, 100.0, 80.0, 30.0);
    auto cyl = kernel.primitives().cylinder({20.0, 20.0, 0.0}, {0.0, 0.0, 1.0}, 10.0, 30.0);
    if (box.status != axiom::StatusCode::Ok || cyl.status != axiom::StatusCode::Ok) {
        std::cerr << "Failed to create primitives\n";
        return 1;
    }

    axiom::BooleanOptions options;
    options.tolerance = kernel.tolerance().global_policy();
    options.diagnostics = true;
    options.auto_repair = true;

    auto result = kernel.booleans().run(axiom::BooleanOp::Subtract, *box.value, *cyl.value, options);
    if (result.status != axiom::StatusCode::Ok || !result.value.has_value()) {
        std::cerr << "Boolean operation failed\n";
        return 1;
    }

    auto props = kernel.query().mass_properties(result.value->output);
    if (props.status != axiom::StatusCode::Ok || !props.value.has_value()) {
        std::cerr << "Mass properties failed\n";
        return 1;
    }

    std::cout << "Output body: " << result.value->output.value << "\n";
    std::cout << "Volume: " << props.value->volume << "\n";
    return 0;
}
