# Force every first-party production object into one coverage mapping. LLVM 18 cannot reliably
# consume coverage sections directly from archives. This executable is never run for hits.
function(wiola_coverage_libraries directory result)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    set(libraries)
    foreach(target IN LISTS targets)
        get_target_property(kind "${target}" TYPE)
        if(kind STREQUAL "STATIC_LIBRARY" AND target MATCHES "^wiola_")
            list(APPEND libraries "${target}")
        endif()
    endforeach()
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        wiola_coverage_libraries("${child}" nested)
        list(APPEND libraries ${nested})
    endforeach()
    set(${result} "${libraries}" PARENT_SCOPE)
endfunction()
wiola_coverage_libraries("${PROJECT_SOURCE_DIR}/src" production_libraries)
add_executable(
    wiola_coverage_inventory
    EXCLUDE_FROM_ALL
    tests/boundaries/coverage_inventory.cpp
)
target_link_libraries(
    wiola_coverage_inventory
    PRIVATE ${production_libraries} wiola_coverage wiola_sanitizer
)
# Put each archive into the link exactly once before the ordinary dependency graph. Applying
# WHOLE_ARCHIVE to transitive target links repeats archives on CMake 3.28 and duplicates symbols.
foreach(library IN LISTS production_libraries)
    target_link_options(
        wiola_coverage_inventory
        PRIVATE
            "LINKER:--whole-archive,$<TARGET_FILE:${library}>,--no-whole-archive"
    )
endforeach()
