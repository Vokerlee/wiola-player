include(GoogleTest)

add_executable(pcm_queue_test pcm_queue_test.cpp)

add_executable(spsc_ring_buffer_test spsc_ring_buffer_test.cpp)

add_executable(cache_line_test cache_line_test.cpp)

add_executable(audio_buffer_source_test buffer_source_test.cpp)

add_executable(playback_relay_test relay_test.cpp)

add_executable(playback_shaped_source_test shaped_source_test.cpp)

foreach(
    test
    IN
    ITEMS
        pcm_queue_test
        spsc_ring_buffer_test
        cache_line_test
        audio_buffer_source_test
        playback_relay_test
        playback_shaped_source_test
)
    target_link_libraries(
        ${test}
        PRIVATE wiola_playback_buffer GTest::gtest_main
    )

    foreach(
        build_control
        IN
        ITEMS wiola_coverage wiola_sanitizer wiola_warnings
    )
        if(TARGET ${build_control})
            target_link_libraries(${test} PRIVATE ${build_control})
        endif()
    endforeach()

    gtest_discover_tests(${test})
endforeach()

target_link_libraries(spsc_ring_buffer_test PRIVATE Threads::Threads)

# The bridge borrows only the Stage port; this case explicitly supplies the application DSP chain.
target_link_libraries(playback_shaped_source_test PRIVATE wiola::audio_dsp)

target_include_directories(
    playback_shaped_source_test
    PRIVATE "${PROJECT_SOURCE_DIR}/tests"
)
