cmake_minimum_required(VERSION 3.20)

project(AxiomKernel VERSION 0.1.0 LANGUAGES CXX)

option(AXM_ENABLE_TESTS "Build tests" ON)
option(AXM_ENABLE_EXAMPLES "Build examples" ON)
option(AXM_ENABLE_BENCHMARKS "Build benchmarks" OFF)
option(AXM_ENABLE_DIAGNOSTICS "Enable diagnostics support" ON)
option(AXM_ENABLE_STRICT_WARNINGS "Enable strict compiler warnings" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/AxiomKernelLibraries.cmake)

if (AXM_ENABLE_EXAMPLES)
    add_executable(axiom_basic_workflow examples/basic_workflow.cpp)
    target_link_libraries(axiom_basic_workflow PRIVATE axiom_kernel)
    add_executable(axiom_minimal_plugin examples/minimal_plugin.cpp)
    target_link_libraries(axiom_minimal_plugin PRIVATE axiom_kernel)
endif()

if (AXM_ENABLE_TESTS)
    enable_testing()
    add_executable(axiom_smoke_test tests/sdk/smoke_test.cpp)
    target_link_libraries(axiom_smoke_test PRIVATE axiom_kernel)
    add_test(NAME axiom_smoke_test COMMAND axiom_smoke_test)

    add_executable(axiom_plugin_sdk_test tests/sdk/plugin_sdk_test.cpp)
    target_link_libraries(axiom_plugin_sdk_test PRIVATE axiom_kernel)
    add_test(NAME axiom_plugin_sdk_test COMMAND axiom_plugin_sdk_test)

    add_executable(axiom_boolean_workflow_test tests/ops/boolean_workflow_test.cpp)
    target_link_libraries(axiom_boolean_workflow_test PRIVATE axiom_kernel)
    add_test(NAME axiom_boolean_workflow_test COMMAND axiom_boolean_workflow_test)

    add_executable(axiom_io_workflow_test tests/io/io_workflow_test.cpp)
    target_link_libraries(axiom_io_workflow_test PRIVATE axiom_kernel)
    add_test(NAME axiom_io_workflow_test COMMAND axiom_io_workflow_test)

    add_executable(axiom_diagnostics_test tests/diag/diagnostics_test.cpp)
    target_link_libraries(axiom_diagnostics_test PRIVATE axiom_kernel)
    add_test(NAME axiom_diagnostics_test COMMAND axiom_diagnostics_test)

    add_executable(axiom_geometry_test tests/geo/geometry_test.cpp)
    target_link_libraries(axiom_geometry_test PRIVATE axiom_kernel)
    add_test(NAME axiom_geometry_test COMMAND axiom_geometry_test)

    add_executable(axiom_topology_test tests/topo/topology_test.cpp)
    target_link_libraries(axiom_topology_test PRIVATE axiom_kernel)
    add_test(NAME axiom_topology_test COMMAND axiom_topology_test)

    add_executable(axiom_representation_io_test tests/rep/representation_io_test.cpp)
    target_link_libraries(axiom_representation_io_test PRIVATE axiom_kernel)
    add_test(NAME axiom_representation_io_test COMMAND axiom_representation_io_test)

    add_executable(axiom_ops_heal_test tests/ops/ops_heal_test.cpp)
    target_link_libraries(axiom_ops_heal_test PRIVATE axiom_kernel)
    add_test(NAME axiom_ops_heal_test COMMAND axiom_ops_heal_test)

    add_executable(axiom_query_eval_test tests/eval/query_eval_test.cpp)
    target_link_libraries(axiom_query_eval_test PRIVATE axiom_kernel)
    add_test(NAME axiom_query_eval_test COMMAND axiom_query_eval_test)

    add_executable(axiom_boolean_prep_test tests/ops/boolean_prep_test.cpp)
    target_link_libraries(axiom_boolean_prep_test PRIVATE axiom_kernel)
    add_test(NAME axiom_boolean_prep_test COMMAND axiom_boolean_prep_test)

    add_executable(axiom_perf_baseline_test tests/perf/perf_baseline_test.cpp)
    target_link_libraries(axiom_perf_baseline_test PRIVATE axiom_kernel)
    add_test(NAME axiom_perf_baseline_test COMMAND axiom_perf_baseline_test)
    set_tests_properties(axiom_perf_baseline_test PROPERTIES TIMEOUT 30)

    add_executable(axiom_math_services_test tests/math/math_services_test.cpp)
    target_link_libraries(axiom_math_services_test PRIVATE axiom_kernel)
    add_test(NAME axiom_math_services_test COMMAND axiom_math_services_test)
endif()
