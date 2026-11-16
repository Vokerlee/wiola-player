wiola_add_library(
    wiola_codec_decode
    SOURCES decoder.cpp format.cpp registry.cpp
    LIBS wiola_codec_ffmpeg wiola_media
)
