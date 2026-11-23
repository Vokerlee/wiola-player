find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(
    wiola_coverage_ownership
    COMMAND
        Python3::Interpreter -m tools.coverage.measurement --build-dir
        ${CMAKE_BINARY_DIR} --cpp-report
        ${WIOLA_COVERAGE_REPORT_DIRECTORY}/summary.json --python-report
        ${WIOLA_COVERAGE_REPORT_DIRECTORY}/python.json --output
        ${WIOLA_COVERAGE_REPORT_DIRECTORY}/owners.json
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    DEPENDS wiola_python_coverage
    VERBATIM
)
