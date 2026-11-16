wiola_add_library(
    wiola_codec_ffmpeg
    SOURCES input.cpp packet_decoder.cpp
    LIBS wiola_media wiola_ffmpeg
)
