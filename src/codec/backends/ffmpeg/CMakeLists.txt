wiola_add_library(
    wiola_codec_ffmpeg
    SOURCES
        decoder.cpp
        input.cpp
        metadata.cpp
        packet_decoder.cpp
        pcm_converter.cpp
    LIBS wiola_media wiola_ffmpeg
)
