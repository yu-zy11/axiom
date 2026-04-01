# AxiomKernel 静态库与聚合 target（由根目录 CMakeLists.txt include）
# 使用 ${CMAKE_CURRENT_SOURCE_DIR}：include 时仍为工程根目录

add_library(axiom_core INTERFACE)
target_compile_features(axiom_core INTERFACE cxx_std_20)
target_include_directories(axiom_core
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (AXM_ENABLE_STRICT_WARNINGS)
    if (MSVC)
        add_library(axiom_warnings INTERFACE)
        target_compile_options(axiom_warnings INTERFACE /W4 /permissive-)
    else()
        add_library(axiom_warnings INTERFACE)
        target_compile_options(axiom_warnings INTERFACE -Wall -Wextra -Wpedantic)
    endif()
endif()

add_library(axiom_diag STATIC
    src/diag/diagnostic_internal_utils.cpp
    src/diag/diagnostic_service.cpp
)
target_link_libraries(axiom_diag PUBLIC axiom_core)
target_include_directories(axiom_diag PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(axiom_math STATIC
    src/math/math_internal_utils.cpp
    src/math/math_services.cpp
)
target_link_libraries(axiom_math PUBLIC axiom_core)
target_include_directories(axiom_math PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(axiom_geo STATIC
    src/geo/geometry_services.cpp
)
target_link_libraries(axiom_geo PUBLIC axiom_core axiom_math axiom_diag)
target_include_directories(axiom_geo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(axiom_topo STATIC
    src/topo/topology_service.cpp
)
target_link_libraries(axiom_topo PUBLIC axiom_core axiom_geo axiom_math axiom_diag)
target_include_directories(axiom_topo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(axiom_rep STATIC
    src/rep/representation_conversion_service.cpp
    src/rep/representation_internal_utils.cpp
)
target_link_libraries(axiom_rep PUBLIC axiom_core axiom_geo axiom_topo axiom_math axiom_diag)
target_include_directories(axiom_rep PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(axiom_ops STATIC
    src/ops/ops_services.cpp
)
target_link_libraries(axiom_ops PUBLIC axiom_core axiom_math axiom_geo axiom_topo axiom_rep axiom_diag)
target_include_directories(axiom_ops PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(axiom_heal STATIC
    src/heal/heal_services.cpp
)
target_link_libraries(axiom_heal PUBLIC axiom_core axiom_math axiom_geo axiom_topo axiom_rep axiom_diag)
target_include_directories(axiom_heal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(axiom_eval STATIC
    src/eval/eval_internal_utils.cpp
    src/eval/eval_services.cpp
)
target_link_libraries(axiom_eval PUBLIC axiom_core axiom_diag)
target_include_directories(axiom_eval PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(axiom_io STATIC
    src/io/io_service.cpp
)
target_link_libraries(axiom_io PUBLIC axiom_core axiom_rep axiom_geo axiom_topo axiom_diag axiom_heal)
target_include_directories(axiom_io PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(axiom_plugin STATIC
    src/plugin/plugin_registry.cpp
)
target_link_libraries(axiom_plugin PUBLIC axiom_core axiom_diag)
target_include_directories(axiom_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(axiom_sdk STATIC
    src/sdk/kernel.cpp
)
target_link_libraries(axiom_sdk
    PUBLIC
        axiom_core
        axiom_diag
        axiom_math
        axiom_geo
        axiom_topo
        axiom_rep
        axiom_ops
        axiom_heal
        axiom_eval
        axiom_io
        axiom_plugin
)
target_include_directories(axiom_sdk PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(axiom_kernel INTERFACE)
target_link_libraries(axiom_kernel
    INTERFACE
        axiom_sdk
)

if (AXM_ENABLE_STRICT_WARNINGS)
    target_link_libraries(axiom_diag PRIVATE axiom_warnings)
    target_link_libraries(axiom_math PRIVATE axiom_warnings)
    target_link_libraries(axiom_geo PRIVATE axiom_warnings)
    target_link_libraries(axiom_topo PRIVATE axiom_warnings)
    target_link_libraries(axiom_rep PRIVATE axiom_warnings)
    target_link_libraries(axiom_ops PRIVATE axiom_warnings)
    target_link_libraries(axiom_heal PRIVATE axiom_warnings)
    target_link_libraries(axiom_eval PRIVATE axiom_warnings)
    target_link_libraries(axiom_io PRIVATE axiom_warnings)
    target_link_libraries(axiom_plugin PRIVATE axiom_warnings)
    target_link_libraries(axiom_sdk PRIVATE axiom_warnings)
endif()

target_compile_definitions(axiom_core
    INTERFACE
        $<$<BOOL:${AXM_ENABLE_DIAGNOSTICS}>:AXM_ENABLE_DIAGNOSTICS=1>
)
