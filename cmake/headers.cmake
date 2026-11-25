include_guard(GLOBAL)

# Authored headers stay beside their implementation. Target-owned build forwarders provide
# qualified includes without exposing another target's headers or duplicating source files.
function(wiola_target_headers target include_prefix)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "" "HEADERS")

    set(build_include "${CMAKE_CURRENT_BINARY_DIR}/${target}/include")

    get_target_property(target_type ${target} TYPE)

    if(target_type STREQUAL "INTERFACE_LIBRARY")
        set(scope INTERFACE)
    else()
        set(scope PUBLIC)
    endif()

    target_include_directories(${target} ${scope} "${build_include}")

    foreach(header IN LISTS arg_HEADERS)
        get_filename_component(header_name "${header}" NAME)

        get_filename_component(source_header "${header}" ABSOLUTE)

        file(
            GENERATE OUTPUT "${build_include}/${include_prefix}/${header_name}"
            CONTENT "#pragma once\n#include \"${source_header}\"\n"
        )

        target_sources(${target} ${scope} "${source_header}")
    endforeach()
endfunction()
