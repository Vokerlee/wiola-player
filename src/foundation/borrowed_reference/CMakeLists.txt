include(GNUInstallDirs)

add_library(wiola_borrowed_reference INTERFACE)
add_library(wiola::borrowed_reference ALIAS wiola_borrowed_reference)
set_target_properties(
    wiola_borrowed_reference
    PROPERTIES EXPORT_NAME borrowed_reference
)
target_compile_features(wiola_borrowed_reference INTERFACE cxx_std_23)
target_include_directories(
    wiola_borrowed_reference
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

install(
    TARGETS wiola_borrowed_reference
    EXPORT wiola_borrowed_reference_targets
    COMPONENT wiola_borrowed_reference
)
install(
    DIRECTORY public/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT wiola_borrowed_reference
    FILES_MATCHING
    PATTERN "*.hpp"
)
install(
    EXPORT wiola_borrowed_reference_targets
    FILE wiola_borrowed_reference-config.cmake
    NAMESPACE wiola::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wiola_borrowed_reference
    COMPONENT wiola_borrowed_reference
)

if(WIOLA_BUILD_TESTS)
    add_executable(borrowed_reference_test tests/borrowed_reference_test.cpp)
    target_link_libraries(
        borrowed_reference_test
        PRIVATE
            wiola::borrowed_reference
            wiola_sanitizer
            wiola_coverage
            wiola_warnings
    )
    add_test(
        NAME BorrowedReference.OwnershipTransfer
        COMMAND borrowed_reference_test
    )

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(
        NAME BorrowedReference.InstalledConsumer
        COMMAND
            Python3::Interpreter
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_export.py --build-dir
            ${CMAKE_BINARY_DIR} --cmake ${CMAKE_COMMAND} --compiler
            ${CMAKE_CXX_COMPILER} --system ${CMAKE_SYSTEM_NAME}
    )
endif()
