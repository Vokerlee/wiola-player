function(wiola_add_qt_tsan_runtime_check consumer)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    if(NOT TARGET "${consumer}")
        message(
            FATAL_ERROR
            "Qt runtime verification needs a configured consumer"
        )
    endif()

    add_custom_target(
        wiola_verify_tsan_runtime
        COMMAND
            Python3::Interpreter -m scripts.qt.tsan.verify --binary
            "$<TARGET_FILE:${consumer}>" --output
            "${CMAKE_BINARY_DIR}/Testing/qt-runtime.txt"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        VERBATIM
    )
endfunction()
