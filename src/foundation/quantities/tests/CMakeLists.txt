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

add_executable(
    quantities_consumer_test
    "${CMAKE_CURRENT_SOURCE_DIR}/consumer.cpp"
)

target_link_libraries(quantities_consumer_test PRIVATE wiola::quantities)

wiola_apply_build_controls(quantities_consumer_test)

add_test(NAME Quantities.Consumer COMMAND quantities_consumer_test)
