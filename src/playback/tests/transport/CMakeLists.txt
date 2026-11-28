include(GoogleTest)

foreach(
    suite
    IN
    ITEMS playback playhead transport_worker player seek_operation
)
    add_executable(engine_${suite}_test ${suite}_test.cpp)

    target_link_libraries(
        engine_${suite}_test
        PRIVATE wiola_playback_transport GTest::gtest_main Threads::Threads
    )

    target_include_directories(
        engine_${suite}_test
        PRIVATE "${PROJECT_SOURCE_DIR}/tests"
    )

    if(COMMAND wiola_apply_build_controls)
        wiola_apply_build_controls(engine_${suite}_test)
    endif()

    gtest_discover_tests(engine_${suite}_test)
endforeach()

target_sources(engine_seek_operation_test PRIVATE seek_plan_test.cpp)
