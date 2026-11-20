wiola_add_test(
    media_track_metadata_test
    SOURCES track_metadata_test.cpp
    LIBS wiola_media
)
wiola_add_test(
    media_byte_stream_test
    SOURCES byte_stream_test.cpp
    LIBS wiola_codec_decode
)
