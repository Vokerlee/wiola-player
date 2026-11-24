add_subdirectory(pipeline)

wiola_add_test(
    engine_fake_decoder_test
    SOURCES fake_decoder_test.cpp
    LIBS wiola::media
)
wiola_add_test(
    engine_tag_reader_test
    SOURCES tag_reader_test.cpp
    LIBS wiola_engine_metadata
)
wiola_add_test(
    engine_metadata_coordinator_test
    SOURCES metadata_coordinator_test.cpp
    LIBS wiola_engine_metadata wiola_playback_queue
)
wiola_add_test(
    engine_session_test
    SOURCES session_test.cpp
    LIBS wiola_engine_session wiola_codec
    DATA "${PROJECT_SOURCE_DIR}/tests/codec/data"
)
