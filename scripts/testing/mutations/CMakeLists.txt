find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(
    wiola_check_mutations
    COMMAND
        Python3::Interpreter -m scripts.testing.mutations --build-dir
        "${CMAKE_BINARY_DIR}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    VERBATIM
)
