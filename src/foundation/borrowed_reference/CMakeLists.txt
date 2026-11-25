include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/headers.cmake")

add_library(wiola_borrowed_reference INTERFACE)

add_library(wiola::borrowed_reference ALIAS wiola_borrowed_reference)

target_compile_features(wiola_borrowed_reference INTERFACE cxx_std_23)

wiola_target_headers(
    wiola_borrowed_reference
    wiola/foundation
    HEADERS borrowed.hpp handle.hpp
)

if(WIOLA_BUILD_TESTS)
    include(GoogleTest)

    add_executable(core_handle_test tests/handle_test.cpp)

    target_link_libraries(
        core_handle_test
        PRIVATE
            wiola::borrowed_reference
            GTest::gtest_main
            wiola_sanitizer
            wiola_coverage
            wiola_warnings
    )

    gtest_discover_tests(core_handle_test)

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

    add_executable(
        borrowed_reference_consumer_test
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/consumer.cpp"
    )

    target_link_libraries(
        borrowed_reference_consumer_test
        PRIVATE wiola::borrowed_reference
    )

    wiola_apply_build_controls(borrowed_reference_consumer_test)

    add_test(
        NAME BorrowedReference.Consumer
        COMMAND borrowed_reference_consumer_test
    )
endif()
