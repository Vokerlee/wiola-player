foreach(
    suite
    IN
    ITEMS
        biquad
        output_sanitizer
        volume
        equalizer
        equalizer_publication
        processing_chain
        relay
        shaped_source
)
    wiola_audio_add_test(audio_${suite}_test "${suite}_test.cpp" wiola::audio_dsp)
endforeach()
# These implementation tests need their owning private protocols.
foreach(target IN ITEMS audio_biquad_test audio_equalizer_publication_test)
    target_include_directories(
        ${target}
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../private"
    )
endforeach()
target_link_libraries(audio_equalizer_publication_test PRIVATE Threads::Threads)

target_link_libraries(
    audio_equalizer_test
    PRIVATE wiola_test_allocation_failure
)
