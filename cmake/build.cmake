# Shared compiler controls and thin declarations for internal implementations and tests.
# Each source owner declares its API headers and actual dependencies.

# A build with no type gets no optimisation at all. That is not a neutral default here: the
# decoder feeds a callback that must answer within a device period, and unoptimised it runs
# several times slower. Anyone who names a type still gets what they asked for.
get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if(NOT is_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set_property(
    CACHE CMAKE_BUILD_TYPE
    PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel
)

# What the project is compiled with. Private to us: a consumer's code is not our business.
add_library(wiola_warnings INTERFACE)

target_compile_options(
    wiola_warnings
    INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall
        -Wextra
        -Wpedantic>
        $<$<CXX_COMPILER_ID:GNU>:-Wno-interference-size>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# Coverage instrumentation, carried by a target of its own so that only what links it is measured:
# the project's code, never a fetched dependency. Empty unless asked for.
add_library(wiola_coverage INTERFACE)

if(ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(
            FATAL_ERROR
            "ENABLE_COVERAGE needs Clang: the flags are LLVM's, not GCC's."
        )
    endif()

    # Counts are per line, so the source has to still be laid out in lines: inlining and hoisting
    # move them elsewhere and the report stops matching what was written.
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(
            WARNING
            "ENABLE_COVERAGE outside a Debug build reports optimized line counts."
        )
    endif()

    target_compile_options(
        wiola_coverage
        INTERFACE -fprofile-instr-generate -fcoverage-mapping
    )

    target_link_options(wiola_coverage INTERFACE -fprofile-instr-generate)
endif()

# A sanitizer, carried the same way and for the same reason: what a fetched dependency does is not
# what we are asking about. Empty unless asked for.
add_library(wiola_sanitizer INTERFACE)

# One process cannot hold both runtimes.
if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ENABLE_ASAN and ENABLE_TSAN are mutually exclusive.")
endif()

if(ENABLE_ASAN OR ENABLE_TSAN)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "The sanitizers need GCC or Clang.")
    endif()

    # Frame pointers and symbols, so a report names the calls that led there.
    target_compile_options(wiola_sanitizer INTERFACE -fno-omit-frame-pointer -g)
endif()

if(ENABLE_ASAN)
    # A tail call is the first thing a stack trace loses, and a use-after-free is read from one.
    target_compile_options(
        wiola_sanitizer
        INTERFACE -fsanitize=address -fno-optimize-sibling-calls
    )

    target_link_options(wiola_sanitizer INTERFACE -fsanitize=address)
endif()

if(ENABLE_TSAN)
    target_compile_options(wiola_sanitizer INTERFACE -fsanitize=thread)

    target_link_options(wiola_sanitizer INTERFACE -fsanitize=thread)
endif()

# Written at configure time, so the version is a typed constant rather than a macro handed to
# every translation unit.
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/version.hpp.in"
    "${PROJECT_BINARY_DIR}/generated/wiola/version.hpp"
    @ONLY
)

# Apply the selected build controls without changing a component's public dependencies.
function(wiola_apply_build_controls target)
    foreach(
        build_control
        IN
        ITEMS wiola_coverage wiola_sanitizer wiola_warnings
    )
        if(TARGET ${build_control})
            target_link_libraries(${target} PRIVATE ${build_control})
        endif()
    endforeach()
endfunction()

# Internal implementations have private checkout access and compiler controls.
# Public APIs and public type dependencies are declared in the owning CMakeLists.txt.
function(wiola_add_library name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBS")

    add_library(${name} STATIC ${arg_SOURCES})

    wiola_apply_build_controls(${name})

    target_compile_features(${name} PUBLIC cxx_std_23)

    target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/src")

    target_link_libraries(${name} PRIVATE ${arg_LIBS})
endfunction()

# Gives `target` the icon Windows shows for it. Does nothing elsewhere, where an executable carries
# no resources of its own.
function(wiola_set_icon target icon)
    if(NOT WIN32)
        return()
    endif()

    enable_language(RC)

    set(resource "${PROJECT_BINARY_DIR}/generated/${target}.rc")

    configure_file("${PROJECT_SOURCE_DIR}/cmake/icon.rc.in" "${resource}" @ONLY)

    # The generated file does not change when the icon does, so the icon is named as a dependency.
    set_source_files_properties(
        "${resource}"
        PROPERTIES OBJECT_DEPENDS "${icon}"
    )

    target_sources(${target} PRIVATE "${resource}")
endfunction()

function(wiola_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "DATA" "SOURCES;LIBS")

    add_executable(${name} ${arg_SOURCES})

    target_link_libraries(
        ${name}
        PRIVATE
            GTest::gtest_main
            wiola_coverage
            wiola_sanitizer
            wiola_warnings
            ${arg_LIBS}
    )

    set_target_properties(
        ${name}
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )

    # Internal tests may inspect implementation headers; public consumers link owned API targets.
    target_include_directories(
        ${name}
        PRIVATE "${PROJECT_SOURCE_DIR}/tests" "${PROJECT_SOURCE_DIR}/src"
    )

    if(arg_DATA)
        target_compile_definitions(
            ${name}
            PRIVATE WIOLA_TEST_DATA_DIR="${arg_DATA}"
        )
    endif()

    include(GoogleTest)

    gtest_discover_tests(${name})
endfunction()
