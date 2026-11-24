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
)
    wiola_audio_add_test(audio_${suite}_test "${suite}_test.cpp" wiola::audio_dsp)
endforeach()
# These implementation tests need their owning private protocols.
foreach(target IN ITEMS audio_biquad_test audio_equalizer_publication_test)
    target_include_directories(
        ${target}
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    )
endforeach()
target_link_libraries(audio_equalizer_publication_test PRIVATE Threads::Threads)

foreach(target IN ITEMS audio_equalizer_test audio_processing_chain_test)
    target_link_libraries(${target} PRIVATE wiola_test_allocation_failure)
endforeach()
target_link_libraries(audio_processing_chain_test PRIVATE Threads::Threads)
