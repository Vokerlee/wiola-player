include_guard(GLOBAL)

# Keep source headers with their owner while preserving namespaced SDK includes. Forwarders are
# build artifacts only: they include the authored files, retaining their dependencies, diagnostics
# and compiler coverage. Installation publishes the actual headers, without source-tree references.
function(wiola_export_headers target include_prefix component)
    cmake_parse_arguments(PARSE_ARGV 3 arg "" "" "HEADERS")
    set(build_include "${CMAKE_CURRENT_BINARY_DIR}/include")
    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        set(scope INTERFACE)
    else()
        set(scope PUBLIC)
    endif()
    target_include_directories(
        ${target}
        ${scope}
        $<BUILD_INTERFACE:${build_include}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    foreach(header IN LISTS arg_HEADERS)
        get_filename_component(header_name "${header}" NAME)
        get_filename_component(source_header "${header}" ABSOLUTE)
        file(
            GENERATE OUTPUT "${build_include}/${include_prefix}/${header_name}"
            CONTENT "#pragma once\n#include \"${source_header}\"\n"
        )
        target_sources(${target} ${scope} "$<BUILD_INTERFACE:${source_header}>")
    endforeach()
    install(
        FILES ${arg_HEADERS}
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${include_prefix}"
        COMPONENT ${component}
    )
endfunction()
