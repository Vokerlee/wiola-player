wiola_add_library(wiola_media_audio_decoder SOURCES audio_decoder.cpp)
add_library(wiola::audio_decoder ALIAS wiola_media_audio_decoder)

if(WIOLA_BUILD_TESTS)
    include(GoogleTest)
    foreach(kind IN ITEMS contract port)
        set(test_target "media_audio_decoder_${kind}_test")
        add_executable(${test_target} "tests/decoder_${kind}_test.cpp")
        target_link_libraries(
            ${test_target}
            PRIVATE
                wiola::audio_decoder
                GTest::gtest_main
                wiola_coverage
                wiola_sanitizer
                wiola_warnings
        )
        gtest_discover_tests(${test_target})
    endforeach()
endif()
