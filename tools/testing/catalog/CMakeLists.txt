find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(test_catalog_arguments)
if(NOT WIOLA_BUILD_GUI)
    list(APPEND test_catalog_arguments --headless)
endif()
add_custom_target(
    wiola_check_test_contracts
    COMMAND
        Python3::Interpreter -m tools.architecture.commands.check_tests
        --build-dir ${CMAKE_BINARY_DIR} ${test_catalog_arguments}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    VERBATIM
)
