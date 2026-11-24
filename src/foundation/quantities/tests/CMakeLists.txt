include(GoogleTest)

add_executable(utils_units_test units_test.cpp)
target_link_libraries(
    utils_units_test
    PRIVATE
        wiola::quantities
        GTest::gtest_main
        wiola_coverage
        wiola_sanitizer
        wiola_warnings
)
gtest_discover_tests(utils_units_test)

wiola_add_public_consumer_test(
    Quantities.InstalledConsumer
    PACKAGE wiola_quantities
    TARGET wiola::quantities
    CONSUMER "${CMAKE_CURRENT_SOURCE_DIR}/consumer.cpp"
    COMPONENTS wiola_quantities
)
