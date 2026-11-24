add_subdirectory(pipeline)

wiola_add_test(
    engine_fake_decoder_test
    SOURCES fake_decoder_test.cpp
    LIBS wiola::media
)
wiola_add_test(
    engine_session_test
    SOURCES session_test.cpp
    LIBS
        wiola_engine_session
        wiola_codec
        wiola_test_allocation_failure
        wiola::audio_dsp
    DATA "${PROJECT_SOURCE_DIR}/tests/codec/data"
)
