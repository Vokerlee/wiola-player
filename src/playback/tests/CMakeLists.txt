add_subdirectory(buffering)

add_subdirectory(transport)

add_subdirectory(loading)

add_subdirectory(queue)

add_subdirectory(metadata)

add_subdirectory(pipeline)

add_subdirectory(session)

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

target_include_directories(
    playback_contract_test
    PRIVATE "${PROJECT_SOURCE_DIR}/tests"
)

include(GoogleTest)

gtest_discover_tests(playback_contract_test)

add_executable(
    playback_consumer_test
    "${CMAKE_CURRENT_SOURCE_DIR}/consumer.cpp"
)

target_link_libraries(playback_consumer_test PRIVATE wiola::playback_api)

wiola_apply_build_controls(playback_consumer_test)

add_test(NAME Playback.Consumer COMMAND playback_consumer_test)

add_executable(
    playback_runtime_consumer_test
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime_consumer.cpp"
)

target_link_libraries(playback_runtime_consumer_test PRIVATE wiola::playback)

wiola_apply_build_controls(playback_runtime_consumer_test)

add_test(NAME PlaybackRuntime.Consumer COMMAND playback_runtime_consumer_test)
