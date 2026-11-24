include(GoogleTest)

add_executable(pcm_queue_test pcm_queue_test.cpp)
add_executable(spsc_ring_buffer_test spsc_ring_buffer_test.cpp)
add_executable(cache_line_test cache_line_test.cpp)
add_executable(audio_buffer_source_test buffer_source_test.cpp)

foreach(
    test
    IN
    ITEMS
        pcm_queue_test
        spsc_ring_buffer_test
        cache_line_test
        audio_buffer_source_test
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
