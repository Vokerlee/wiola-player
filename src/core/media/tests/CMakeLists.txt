include(GoogleTest)

function(wiola_media_add_test target source)
    add_executable(${target} ${source})
    target_link_libraries(
        ${target}
        PRIVATE
            wiola::media
            GTest::gtest_main
            wiola_coverage
            wiola_sanitizer
            wiola_warnings
    )
    gtest_discover_tests(${target})
endfunction()

wiola_media_add_test(media_audio_decoder_contract_test decoder_contract_test.cpp)
wiola_media_add_test(media_audio_decoder_port_test decoder_port_test.cpp)
wiola_media_add_test(media_track_metadata_test track_metadata_test.cpp)
wiola_media_add_test(media_metadata_writer_test metadata_writer_test.cpp)
wiola_media_add_test(pcm_stream_spec_test stream_spec_test.cpp)

wiola_add_public_consumer_test(
    Media.InstalledConsumer
    "${CMAKE_CURRENT_SOURCE_DIR}/public_package.json"
)
