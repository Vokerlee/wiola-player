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

wiola_add_public_consumer_test(
    AudioOutput.InstalledConsumer
    PACKAGE wiola_audio
    TARGET wiola::audio_output
    CONSUMER "${CMAKE_CURRENT_SOURCE_DIR}/consumer.cpp"
    COMPONENTS wiola_borrowed_reference wiola_quantities wiola_media wiola_audio
)
wiola_add_public_consumer_test(
    AudioProcessing.InstalledConsumer
    PACKAGE wiola_audio
    TARGET wiola::audio_dsp
    CONSUMER "${CMAKE_CURRENT_SOURCE_DIR}/processing/consumer.cpp"
    COMPONENTS
        wiola_borrowed_reference
        wiola_quantities
        wiola_media
        wiola_audio
        wiola_audio_dsp
)
