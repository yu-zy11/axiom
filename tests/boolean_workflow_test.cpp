#include <iostream>

#include "axiom/sdk/kernel.h"

int main() {
    axiom::Kernel kernel;

    auto a = kernel.primitives().box({0.0, 0.0, 0.0}, 100.0, 80.0, 30.0);
    auto b = kernel.primitives().cylinder({20.0, 20.0, 0.0}, {0.0, 0.0, 1.0}, 10.0, 30.0);
    if (a.status != axiom::StatusCode::Ok || b.status != axiom::StatusCode::Ok ||
        !a.value.has_value() || !b.value.has_value()) {
        std::cerr << "failed to create boolean inputs\n";
        return 1;
    }

    axiom::BooleanOptions options;
    options.tolerance = kernel.tolerance().global_policy();
    options.diagnostics = true;
    options.auto_repair = true;

    auto result = kernel.booleans().run(axiom::BooleanOp::Subtract, *a.value, *b.value, options);
    if (result.status != axiom::StatusCode::Ok || !result.value.has_value()) {
        std::cerr << "boolean run failed\n";
        return 1;
    }

    auto valid = kernel.validate().validate_all(result.value->output, axiom::ValidationMode::Standard);
    auto strict_valid = kernel.validate().validate_topology(result.value->output, axiom::ValidationMode::Strict);
    auto owned_shells = kernel.topology().query().shells_of_body(result.value->output);
    std::vector<axiom::FaceId> owned_faces;
    if (owned_shells.status == axiom::StatusCode::Ok && owned_shells.value.has_value() && owned_shells.value->size() == 1) {
        auto shell_faces = kernel.topology().query().faces_of_shell(owned_shells.value->front());
        if (shell_faces.status == axiom::StatusCode::Ok && shell_faces.value.has_value()) {
            owned_faces = *shell_faces.value;
        }
    }
    if (valid.status != axiom::StatusCode::Ok ||
        strict_valid.status != axiom::StatusCode::Ok ||
        owned_shells.status != axiom::StatusCode::Ok || !owned_shells.value.has_value() ||
        owned_shells.value->size() != 1 ||
        owned_faces.size() != 6) {
        std::cerr << "boolean output validation failed\n";
        return 1;
    }

    auto props = kernel.query().mass_properties(result.value->output);
    if (props.status != axiom::StatusCode::Ok || !props.value.has_value() || props.value->volume <= 0.0) {
        std::cerr << "invalid boolean output mass properties\n";
        return 1;
    }

    auto source_bodies = kernel.topology().query().source_bodies_of_body(result.value->output);
    auto source_faces = kernel.topology().query().source_faces_of_body(result.value->output);
    if (source_bodies.status != axiom::StatusCode::Ok || !source_bodies.value.has_value() ||
        source_bodies.value->size() != 2 ||
        source_faces.status != axiom::StatusCode::Ok || !source_faces.value.has_value() ||
        !source_faces.value->empty()) {
        std::cerr << "boolean provenance query failed\n";
        return 1;
    }

    const bool has_a = source_bodies.value->at(0).value == a.value->value || source_bodies.value->at(1).value == a.value->value;
    const bool has_b = source_bodies.value->at(0).value == b.value->value || source_bodies.value->at(1).value == b.value->value;
    if (!has_a || !has_b) {
        std::cerr << "boolean provenance does not include both source bodies\n";
        return 1;
    }

    auto disjoint_a = kernel.primitives().box({0.0, 0.0, 0.0}, 5.0, 5.0, 5.0);
    auto disjoint_b = kernel.primitives().box({20.0, 20.0, 20.0}, 3.0, 3.0, 3.0);
    if (disjoint_a.status != axiom::StatusCode::Ok || disjoint_b.status != axiom::StatusCode::Ok ||
        !disjoint_a.value.has_value() || !disjoint_b.value.has_value()) {
        std::cerr << "failed to create disjoint boolean inputs\n";
        return 1;
    }

    axiom::BooleanOptions silent_options;
    silent_options.diagnostics = false;
    auto silent_union = kernel.booleans().run(axiom::BooleanOp::Union, *disjoint_a.value, *disjoint_b.value, silent_options);
    if (silent_union.status != axiom::StatusCode::Ok || !silent_union.value.has_value() ||
        silent_union.value->diagnostic_id.value != 0 || silent_union.diagnostic_id.value != 0 ||
        silent_union.value->warnings.empty()) {
        std::cerr << "boolean diagnostics option did not suppress success diagnostics as expected\n";
        return 1;
    }

    return 0;
}
