set(fixtures "${CMAKE_CURRENT_SOURCE_DIR}/data")

wiola_add_test(
    codec_ffmpeg_build_test
    SOURCES ffmpeg_build_test.cpp
    LIBS wiola_ffmpeg
    DATA "${fixtures}"
)
wiola_add_test(
    codec_ffmpeg_io_test
    SOURCES ffmpeg_io_test.cpp
    LIBS wiola_codec_ffmpeg wiola_ffmpeg
    DATA "${fixtures}"
)
wiola_add_test(
    codec_ffmpeg_packet_decoder_test
    SOURCES ffmpeg_packet_decoder_test.cpp
    LIBS wiola_codec_ffmpeg wiola_ffmpeg
    DATA "${fixtures}"
)
wiola_add_test(
    codec_ffmpeg_ogg_test
    SOURCES ffmpeg_ogg_test.cpp
    LIBS wiola_codec wiola_codec_ffmpeg wiola_ffmpeg
    DATA "${fixtures}"
)
wiola_add_test(
    codec_ffmpeg_mp4_test
    SOURCES ffmpeg_mp4_test.cpp
    LIBS wiola_codec wiola_codec_ffmpeg wiola_ffmpeg
    DATA "${fixtures}"
)
wiola_add_test(
    codec_ffmpeg_adts_test
    SOURCES ffmpeg_adts_test.cpp
    LIBS wiola_codec wiola_codec_ffmpeg wiola_ffmpeg
    DATA "${fixtures}"
)
wiola_add_test(
    codec_ffmpeg_sampled_file_test
    SOURCES ffmpeg_sampled_file_test.cpp
    LIBS wiola_codec wiola_codec_ffmpeg wiola_ffmpeg
    DATA "${fixtures}"
)
wiola_add_test(
    codec_ffmpeg_lossless_test
    SOURCES ffmpeg_lossless_test.cpp
    LIBS wiola_codec wiola_codec_ffmpeg wiola_ffmpeg
    DATA "${fixtures}"
)
wiola_add_test(
    codec_ffmpeg_container_test
    SOURCES ffmpeg_container_test.cpp
    LIBS wiola_codec wiola_codec_ffmpeg wiola_ffmpeg
    DATA "${fixtures}"
)
wiola_add_test(
    codec_ffmpeg_channel_layout_test
    SOURCES ffmpeg_channel_layout_test.cpp
    LIBS wiola_codec wiola_codec_ffmpeg wiola_ffmpeg
    DATA "${fixtures}"
)
wiola_add_test(
    codec_ffmpeg_hardening_test
    SOURCES ffmpeg_hardening_test.cpp
    LIBS wiola_codec wiola_codec_ffmpeg wiola_ffmpeg
    DATA "${fixtures}"
)
wiola_add_test(
    codec_ffmpeg_qualification_test
    SOURCES ffmpeg_qualification_test.cpp
    LIBS wiola_codec wiola_codec_ffmpeg wiola_ffmpeg
    DATA "${fixtures}"
)
wiola_add_test(
    codec_ffmpeg_metadata_test
    SOURCES ffmpeg_metadata_test.cpp
    LIBS wiola_codec_ffmpeg wiola_codec_provider wiola_ffmpeg
    DATA "${fixtures}"
)
wiola_add_test(
    codec_ffmpeg_metadata_writer_test
    SOURCES ffmpeg_metadata_writer_test.cpp
    LIBS wiola_codec_ffmpeg wiola_codec_provider wiola_ffmpeg
    DATA "${fixtures}"
)
wiola_add_test(
    codec_ffmpeg_pcm_test
    SOURCES ffmpeg_pcm_test.cpp
    LIBS wiola_codec_ffmpeg wiola_codec_decode wiola_ffmpeg
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
