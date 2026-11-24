foreach(
    suite
    IN
    ITEMS
        biquad
        output_sanitizer
        volume
        equalizer
        processing_chain
        relay
        shaped_source
)
    wiola_audio_add_test(audio_${suite}_test "${suite}_test.cpp" wiola::audio_dsp)
endforeach()
# Only this implementation test needs the private filter primitive.
target_include_directories(
    audio_biquad_test
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../private"
)
