include(headers)

add_library(wiola_audio_miniaudio STATIC device.cpp)

add_library(wiola::audio_miniaudio ALIAS wiola_audio_miniaudio)

set_target_properties(wiola_audio_miniaudio PROPERTIES CXX_EXTENSIONS OFF)

target_link_libraries(
    wiola_audio_miniaudio
    PUBLIC wiola::audio_output
    PRIVATE miniaudio
)

wiola_target_headers(
    wiola_audio_miniaudio
    wiola/audio/miniaudio
    HEADERS device.hpp
)

wiola_apply_build_controls(wiola_audio_miniaudio)

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
