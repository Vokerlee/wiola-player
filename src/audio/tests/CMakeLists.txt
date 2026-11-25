include(GoogleTest)

find_package(Threads REQUIRED)

function(wiola_audio_add_test target source dependency)
    add_executable(${target} ${source})

    target_link_libraries(
        ${target}
        PRIVATE
            ${dependency}
            GTest::gtest_main
            wiola_sanitizer
            wiola_coverage
            wiola_warnings
    )

    # Shared controlled audio collaborators; never an exported production include root.
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/tests")

    gtest_discover_tests(${target})
endfunction()

wiola_audio_add_test(audio_output_test output_test.cpp wiola::audio_output)

target_link_libraries(audio_output_test PRIVATE Threads::Threads)

wiola_audio_add_test(audio_tone_test tone_test.cpp wiola::audio_output)

add_subdirectory(processing)

add_executable(
    audio_output_consumer_test
    "${CMAKE_CURRENT_SOURCE_DIR}/consumer.cpp"
)

target_link_libraries(audio_output_consumer_test PRIVATE wiola::audio_output)

wiola_apply_build_controls(audio_output_consumer_test)

add_test(NAME AudioOutput.Consumer COMMAND audio_output_consumer_test)

add_executable(
    audio_processing_consumer_test
    "${CMAKE_CURRENT_SOURCE_DIR}/processing/consumer.cpp"
)

target_link_libraries(audio_processing_consumer_test PRIVATE wiola::audio_dsp)

wiola_apply_build_controls(audio_processing_consumer_test)

add_test(NAME AudioProcessing.Consumer COMMAND audio_processing_consumer_test)
