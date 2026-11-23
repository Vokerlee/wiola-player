find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(
    wiola_python_coverage
    COMMAND
        Python3::Interpreter -m tools.coverage.python_runner --build-dir
        ${CMAKE_BINARY_DIR} --output
        ${WIOLA_COVERAGE_REPORT_DIRECTORY}/python.json
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    VERBATIM
)
