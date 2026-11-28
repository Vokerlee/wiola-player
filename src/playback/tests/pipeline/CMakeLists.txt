add_executable(
    engine_pipeline_ownership_test
    pipeline_ownership_test.cpp
    source_attachment_test.cpp
)

target_link_libraries(
    engine_pipeline_ownership_test
    PRIVATE
        wiola_playback_pipeline
        wiola_test_allocation_failure
        wiola::audio_dsp
        GTest::gtest_main
)

target_include_directories(
    engine_pipeline_ownership_test
    PRIVATE "${PROJECT_SOURCE_DIR}/tests"
)

if(COMMAND wiola_apply_build_controls)
    wiola_apply_build_controls(engine_pipeline_ownership_test)
endif()

include(GoogleTest)

gtest_discover_tests(engine_pipeline_ownership_test)
