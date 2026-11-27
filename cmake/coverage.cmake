# LLVM reads mappings directly from instrumented objects, including code no test links.
# Follow the existing instrumentation links instead of maintaining a source/target inventory.
function(wiola_coverage_targets directory result)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)

    set(instrumented)

    foreach(target IN LISTS targets)
        get_target_property(links "${target}" LINK_LIBRARIES)

        if(wiola_coverage IN_LIST links)
            list(APPEND instrumented "${target}")
        endif()
    endforeach()

    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)

    foreach(child IN LISTS children)
        wiola_coverage_targets("${child}" nested)

        list(APPEND instrumented ${nested})
    endforeach()

    set(${result} "${instrumented}" PARENT_SCOPE)
endfunction()

wiola_coverage_targets("${PROJECT_SOURCE_DIR}" coverage_targets)

set(coverage_objects)

set(object_flag)

foreach(target IN LISTS coverage_targets)
    get_target_property(kind "${target}" TYPE)

    if(kind STREQUAL "EXECUTABLE")
        set(objects "$<TARGET_FILE:${target}>")
    else()
        set(objects "$<TARGET_OBJECTS:${target}>")
    endif()

    string(
        APPEND coverage_objects
        "\"${object_flag}$<JOIN:${objects},\"\n\"-object=>\"\n"
    )

    set(object_flag "-object=")
endforeach()

set(coverage_response "${PROJECT_BINARY_DIR}/coverage-objects-$<CONFIG>.rsp")

file(GENERATE OUTPUT "${coverage_response}" CONTENT "${coverage_objects}")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_target(
    coverage
    COMMAND
        "${Python3_EXECUTABLE}" -m scripts.coverage --build-dir
        "${PROJECT_BINARY_DIR}" --objects "${coverage_response}" --source
        "${PROJECT_SOURCE_DIR}/src"
    DEPENDS ${coverage_targets}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Run tests and generate LLVM coverage reports"
    USES_TERMINAL
    VERBATIM
)
