add_executable(engine_playlist_test playlist_test.cpp playback_order_test.cpp)
target_link_libraries(
    engine_playlist_test
    PRIVATE wiola_playback_queue GTest::gtest_main wiola_test_allocation_failure
)
if(COMMAND wiola_apply_build_controls)
    wiola_apply_build_controls(engine_playlist_test)
endif()
include(GoogleTest)
gtest_discover_tests(engine_playlist_test)
