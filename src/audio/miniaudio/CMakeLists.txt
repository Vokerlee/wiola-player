wiola_add_library(
    wiola_audio_miniaudio
    SOURCES device.cpp
    LIBS miniaudio wiola_audio_output wiola_audio_dsp
)
add_library(wiola::audio_miniaudio ALIAS wiola_audio_miniaudio)
target_link_libraries(wiola_audio_miniaudio PUBLIC wiola::audio_output)

if(WIOLA_BUILD_TESTS)
    add_executable(audio_device_test tests/device_test.cpp)
    target_link_libraries(
        audio_device_test
        PRIVATE
            wiola::audio_miniaudio
            GTest::gtest_main
            wiola_sanitizer
            wiola_coverage
            wiola_warnings
    )
    target_include_directories(
        audio_device_test
        PRIVATE "${PROJECT_SOURCE_DIR}/tests"
    )
    include(GoogleTest)
    gtest_discover_tests(audio_device_test)
endif()
