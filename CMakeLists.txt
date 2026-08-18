cmake_minimum_required(VERSION 3.20)

project(pleat_state VERSION 0.1.0 LANGUAGES CXX)

option(PLEAT_BUILD_TESTS "Build the native simulation tests" ON)

add_library(pleat_core STATIC
    src/Cloth.cpp
)
add_library(pleat::core ALIAS pleat_core)

target_compile_features(pleat_core PUBLIC cxx_std_20)
target_include_directories(pleat_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
set_target_properties(pleat_core PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

add_library(pleat_wasm_api STATIC
    src/wasm_bindings.cpp
)
add_library(pleat::wasm_api ALIAS pleat_wasm_api)
target_compile_features(pleat_wasm_api PUBLIC cxx_std_20)
target_include_directories(pleat_wasm_api
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(pleat_wasm_api PUBLIC pleat::core)
set_target_properties(pleat_wasm_api PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(pleat_core PRIVATE /W4 /permissive-)
    target_compile_options(pleat_wasm_api PRIVATE /W4 /permissive-)
else()
    target_compile_options(pleat_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(pleat_wasm_api PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(PLEAT_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(pleat_tests tests/test_cloth.cpp)
    target_compile_features(pleat_tests PRIVATE cxx_std_20)
    target_link_libraries(pleat_tests PRIVATE pleat::core pleat::wasm_api)
    set_target_properties(pleat_tests PROPERTIES CXX_EXTENSIONS OFF)

    if(MSVC)
        target_compile_options(pleat_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(pleat_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()

    add_test(NAME pleat_native COMMAND pleat_tests)
endif()
