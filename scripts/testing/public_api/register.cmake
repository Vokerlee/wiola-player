function(wiola_add_public_consumer_test name manifest)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(arguments)
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND arguments --toolchain "${CMAKE_TOOLCHAIN_FILE}")
    endif()
    if(NOT CMAKE_CROSSCOMPILING)
        list(APPEND arguments --run)
    endif()
    add_test(
        NAME ${name}
        COMMAND
            Python3::Interpreter -m scripts.testing.public_api --manifest
            "${manifest}" --build-dir "${CMAKE_BINARY_DIR}" --cmake
            "${CMAKE_COMMAND}" --compiler "${CMAKE_CXX_COMPILER}" --system
            "${CMAKE_SYSTEM_NAME}" --output
            "${CMAKE_CURRENT_BINARY_DIR}/public-consumer-evidence" ${arguments}
    )
    set_tests_properties(
        ${name}
        PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    )
endfunction()
