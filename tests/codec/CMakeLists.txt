wiola_add_test(
    codec_tags_test
    SOURCES tags_test.cpp
    LIBS wiola_codec tag
    DATA "${PROJECT_SOURCE_DIR}/tests/codec/data"
)
set(fixtures "${CMAKE_CURRENT_SOURCE_DIR}/data")

wiola_add_test(
    codec_wav_reader_test
    SOURCES wav_reader_test.cpp
    LIBS wiola_codec
    DATA "${fixtures}"
)
wiola_add_test(
    codec_flac_reader_test
    SOURCES flac_reader_test.cpp
    LIBS wiola_codec
    DATA "${fixtures}"
)
wiola_add_test(
    codec_mp3_reader_test
    SOURCES mp3_reader_test.cpp
    LIBS wiola_codec
    DATA "${fixtures}"
)
wiola_add_test(codec_format_test SOURCES format_test.cpp LIBS wiola_codec)
wiola_add_test(codec_registry_test SOURCES registry_test.cpp LIBS wiola_codec)
wiola_add_test(
    codec_backend_qualification_test
    SOURCES backend_qualification_test.cpp
    LIBS wiola_codec
)
wiola_add_test(
    codec_provider_test
    SOURCES provider_test.cpp
    LIBS wiola_codec
    DATA "${fixtures}"
)
wiola_add_test(
    codec_audio_decoder_test
    SOURCES audio_decoder_test.cpp
    LIBS wiola_codec
    DATA "${fixtures}"
)
wiola_add_test(
    codec_decoder_test
    SOURCES decoder_test.cpp
    LIBS wiola_codec
    DATA "${fixtures}"
)
