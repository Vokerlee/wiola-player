add_executable(engine_playlist_test playlist_test.cpp)
target_link_libraries(
    engine_playlist_test
    PRIVATE wiola_playback_queue GTest::gtest_main
)
if(COMMAND wiola_apply_build_controls)
    wiola_apply_build_controls(engine_playlist_test)
endif()
include(GoogleTest)
gtest_discover_tests(engine_playlist_test)

# TSan provides strong allocation replacements of its own. Its normal queue cases still run;
# the single-thread fault-injection cases run with the standard replacements in Debug/ASan.
if(ENABLE_TSAN)
    target_compile_definitions(
        engine_playlist_test
        PRIVATE WIOLA_TEST_ALLOCATOR_REPLACEMENT=0
    )
else()
    target_sources(engine_playlist_test PRIVATE allocation_failure.cpp)
    target_compile_definitions(
        engine_playlist_test
        PRIVATE WIOLA_TEST_ALLOCATOR_REPLACEMENT=1
    )
endif()
