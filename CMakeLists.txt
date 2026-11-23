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
cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Project modules are included by name, not by path.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(ENABLE_UNIT_TESTS "Build unit tests" ON)
option(WIOLA_BUILD_GUI "Build the Qt GUI and player executable" ON)
option(ENABLE_COVERAGE "Instrument the project's code for coverage" OFF)

option(ENABLE_ASAN "Build the project's code with the address sanitizer" OFF)
option(ENABLE_TSAN "Build the project's code with the thread sanitizer" OFF)

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()

# One answer, read by the dependency list as well as by this file.
if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING AND ENABLE_UNIT_TESTS)
    set(WIOLA_BUILD_TESTS ON)
endif()

include(build)
include(dependencies)

add_subdirectory(src)

if(ENABLE_COVERAGE)
    include(coverage_inventory)
    add_subdirectory(tools/coverage)
endif()

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
    add_subdirectory(tools/testing/catalog)
    if(WIOLA_BUILD_GUI AND NOT CMAKE_CROSSCOMPILING)
        add_subdirectory(tools/testing/mutation_checks)
    endif()
endif()
