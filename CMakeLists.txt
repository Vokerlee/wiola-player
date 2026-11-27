cmake_minimum_required(VERSION 3.28)

project(
    wiola-player
    VERSION 0.3.0
    DESCRIPTION "Wiola media player"
    LANGUAGES CXX
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use: cmake -B build")
endif()

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Project modules are included by name, not by path.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(ENABLE_UNIT_TESTS "Build unit tests" ON)

option(WIOLA_BUILD_GUI "Build the Qt GUI and player executable" ON)

option(
    WIOLA_BUILD_BENCHMARKS
    "Build optional native performance measurements"
    OFF
)

option(ENABLE_COVERAGE "Instrument the project's code for coverage" OFF)

option(ENABLE_ASAN "Build the project's code with the address sanitizer" OFF)

option(ENABLE_TSAN "Build the project's code with the thread sanitizer" OFF)

include(components)

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()

# One answer, read by the dependency list as well as by this file.
set(WIOLA_BUILD_TESTS OFF)

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING AND ENABLE_UNIT_TESTS)
    set(WIOLA_BUILD_TESTS ON)
endif()

include(build)

include(dependencies)

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests/support)
endif()

add_subdirectory(src)

if(WIOLA_BUILD_TESTS AND NOT WIOLA_COMPONENT_BUILD)
    add_subdirectory(tests)

    if(WIOLA_BUILD_GUI AND NOT CMAKE_CROSSCOMPILING AND ENABLE_TSAN)
        add_subdirectory(scripts/qt/tsan)

        wiola_add_qt_tsan_runtime_check(gui_desktop_journey_test)
    endif()
endif()

if(ENABLE_COVERAGE)
    cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2)

    include(coverage)
endif()

if(WIOLA_BUILD_BENCHMARKS)
    get_property(benchmark_targets GLOBAL PROPERTY WIOLA_BENCHMARK_TARGETS)

    set(benchmark_commands)

    foreach(target IN LISTS benchmark_targets)
        list(APPEND benchmark_commands COMMAND "$<TARGET_FILE:${target}>")
    endforeach()

    add_custom_target(
        benchmarks
        ${benchmark_commands}
        DEPENDS ${benchmark_targets}
        COMMENT "Run selected native benchmarks sequentially"
        USES_TERMINAL
        VERBATIM
    )
endif()
