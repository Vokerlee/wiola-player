wiola_add_library(
    wiola_codec_decode
    SOURCES decoder.cpp format.cpp open.cpp registry.cpp
    LIBS wiola_codec_dr_libs wiola_media
)
