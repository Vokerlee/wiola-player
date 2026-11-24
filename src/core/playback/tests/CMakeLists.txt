add_subdirectory(buffering)
add_subdirectory(transport)

add_executable(playback_contract_test contract_test.cpp)
target_link_libraries(
    playback_contract_test
    PRIVATE
        wiola::playback_api
        GTest::gtest_main
        wiola_coverage
        wiola_sanitizer
        wiola_warnings
)
include(GoogleTest)
gtest_discover_tests(playback_contract_test)

wiola_add_public_consumer_test(
    Playback.InstalledConsumer
    PACKAGE wiola_playback
    TARGET wiola::playback_api
    CONSUMER "${CMAKE_CURRENT_SOURCE_DIR}/consumer.cpp"
    COMPONENTS wiola_quantities wiola_media wiola_playback
)
