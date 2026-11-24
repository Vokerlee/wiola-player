include(GoogleTest)
find_package(Threads REQUIRED)

add_executable(audio_output_test output_test.cpp)
target_link_libraries(
    audio_output_test
    PRIVATE
        wiola::audio_output
        GTest::gtest_main
        Threads::Threads
        wiola_sanitizer
        wiola_coverage
        wiola_warnings
)
# Shared controlled output; this test-only include root is never exported by the component.
target_include_directories(
    audio_output_test
    PRIVATE "${PROJECT_SOURCE_DIR}/tests"
)
gtest_discover_tests(audio_output_test)

wiola_add_public_consumer_test(
    AudioOutput.InstalledConsumer
    PACKAGE wiola_audio
    TARGET wiola::audio_output
    CONSUMER "${CMAKE_CURRENT_SOURCE_DIR}/consumer.cpp"
    COMPONENTS wiola_borrowed_reference wiola_quantities wiola_media wiola_audio
)
