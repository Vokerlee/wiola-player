add_executable(
    engine_session_test
    commands_test.cpp
    handoff_test.cpp
    admission_test.cpp
    lifecycle_model_test.cpp
)

target_link_libraries(
    engine_session_test
    PRIVATE
        wiola::playback
        wiola_test_allocation_failure
        wiola::audio_dsp
        GTest::gtest_main
)

target_include_directories(
    engine_session_test
    PRIVATE "${PROJECT_SOURCE_DIR}/tests"
)

if(COMMAND wiola_apply_build_controls)
    wiola_apply_build_controls(engine_session_test)
endif()

include(GoogleTest)

gtest_discover_tests(engine_session_test)
