cmake_minimum_required(VERSION 3.20)
project(FastITCH VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ENABLE_BENCHMARKS "Build benchmark suite" ON)
option(BUILD_TESTING "Build unit and integration tests" ON)

# Strict warning flags
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
    )
    if (ENABLE_SANITIZERS)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Core library
add_library(fastitch_core INTERFACE)
target_include_directories(fastitch_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Implementation library for non-header-only sources
file(GLOB_RECURSE FASTITCH_SOURCES "src/*.cpp")
# Filter out main.cpp from library
list(FILTER FASTITCH_SOURCES EXCLUDE REGEX "src/main\\.cpp$")

if (FASTITCH_SOURCES)
    add_library(fastitch STATIC ${FASTITCH_SOURCES})
    target_link_libraries(fastitch PUBLIC fastitch_core)
else()
    # Header-only fallback until cpp files are added
    add_library(fastitch INTERFACE)
    target_link_libraries(fastitch INTERFACE fastitch_core)
endif()

# Main executable
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
    add_executable(fastitch_bin src/main.cpp)
    set_target_properties(fastitch_bin PROPERTIES OUTPUT_NAME fastitch)
    target_link_libraries(fastitch_bin PRIVATE fastitch)
endif()

# Benchmarks
if (ENABLE_BENCHMARKS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/fastitch_bench.cpp")
    add_executable(fastitch_bench benchmarks/fastitch_bench.cpp)
    target_link_libraries(fastitch_bench PRIVATE fastitch fastitch_core)
endif()

# Testing
if (BUILD_TESTING)
    enable_testing()
    
    file(GLOB_RECURSE TEST_SOURCES "tests/unit/*.cpp" "tests/integration/*.cpp" "tests/property/*.cpp")
    if (TEST_SOURCES)
        add_executable(fastitch_tests ${TEST_SOURCES})
        target_include_directories(fastitch_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(fastitch_tests PRIVATE fastitch fastitch_core)
        add_test(NAME fastitch_unit_tests COMMAND fastitch_tests)
    endif()
endif()
