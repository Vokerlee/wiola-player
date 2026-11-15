wiola_add_library(
    wiola_codec_dr_libs
    SOURCES flac_reader.cpp mp3_reader.cpp wav_reader.cpp
    LIBS wiola_media
)

target_include_directories(
    wiola_codec_dr_libs
    SYSTEM
    PRIVATE "${dr_libs_SOURCE_DIR}"
)
