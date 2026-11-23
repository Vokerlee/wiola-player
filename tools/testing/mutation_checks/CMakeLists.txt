find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(
    wiola_check_mutations
    COMMAND
        Python3::Interpreter -m tools.testing.mutation_checks --build-dir
        "${CMAKE_BINARY_DIR}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    VERBATIM
)
