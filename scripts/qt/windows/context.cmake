# Read the selected toolchain rather than duplicating its compiler choice in CI.
find_program(git_command NAMES git REQUIRED)

execute_process(
    COMMAND
        "${git_command}" -C "${CMAKE_CURRENT_LIST_DIR}" rev-parse
        --show-toplevel
    OUTPUT_VARIABLE repository
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

if(
    DEFINED ENV{WIOLA_QT_TOOLCHAIN_FILE}
    AND NOT "$ENV{WIOLA_QT_TOOLCHAIN_FILE}" STREQUAL ""
)
    set(toolchain "$ENV{WIOLA_QT_TOOLCHAIN_FILE}")
else()
    set(toolchain "${repository}/cmake/mingw-w64.cmake")
endif()

include("${toolchain}")

file(SHA256 "${toolchain}" toolchain_identity)

find_program(host_qmake NAMES qmake6 REQUIRED)

execute_process(
    COMMAND "${host_qmake}" -query QT_VERSION
    OUTPUT_VARIABLE qt_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

execute_process(
    COMMAND "${host_qmake}" -query QT_INSTALL_PREFIX
    OUTPUT_VARIABLE host_prefix
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

execute_process(
    COMMAND "${host_qmake}" -query QT_INSTALL_LIBS
    OUTPUT_VARIABLE host_libraries
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -dumpfullversion -dumpversion
    OUTPUT_VARIABLE compiler_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -dumpmachine
    OUTPUT_VARIABLE compiler_target
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

if(NOT compiler_target STREQUAL "x86_64-w64-mingw32")
    message(
        FATAL_ERROR
        "The Windows SDK supports x86-64 MinGW only: ${compiler_target}"
    )
endif()

find_program(host_python NAMES python3 REQUIRED)

execute_process(
    COMMAND "${host_python}" -m scripts.qt.source "${qt_version}" --identity
    WORKING_DIRECTORY "${repository}"
    OUTPUT_VARIABLE source_identity
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

string(
    SHA256 identity
    "${toolchain_identity}\n${source_identity}\n${qt_version}\n${host_prefix}\n${host_libraries}\n${compiler_version}\n${compiler_target}"
)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E echo "${identity}"
    COMMAND_ERROR_IS_FATAL ANY
)
