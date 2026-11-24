function(wiola_add_public_consumer_test name)
    cmake_parse_arguments(
        PARSE_ARGV 1
        arg
        ""
        "PACKAGE;TARGET;CONSUMER"
        "COMPONENTS"
    )
    if(
        NOT arg_PACKAGE
        OR NOT arg_TARGET
        OR NOT arg_CONSUMER
        OR NOT arg_COMPONENTS
    )
        message(
            FATAL_ERROR
            "Public package tests need a package, target, consumer and install components"
        )
    endif()
    set(consumer_toolchain "${CMAKE_TOOLCHAIN_FILE}")
    if(consumer_toolchain)
        cmake_path(
            ABSOLUTE_PATH consumer_toolchain
            BASE_DIRECTORY "${CMAKE_SOURCE_DIR}"
        )
    endif()
    set(consumer_fixture "${CMAKE_CURRENT_BINARY_DIR}/${name}.consumer.cmake")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/PublicConsumer.cmake.in"
        "${consumer_fixture}"
        @ONLY
    )
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CheckPublicPackage.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${name}.cmake"
        @ONLY
    )
    add_test(
        NAME ${name}
        COMMAND
            "${CMAKE_COMMAND}" "-DWIOLA_CONSUMER_CONFIG=$<CONFIG>" -P
            "${CMAKE_CURRENT_BINARY_DIR}/${name}.cmake"
    )
endfunction()
