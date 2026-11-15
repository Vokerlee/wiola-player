wiola_add_library(
    wiola_codec_decode
    SOURCES
        decoder.cpp
        flac_reader.cpp
        format.cpp
        mp3_reader.cpp
        open.cpp
        wav_reader.cpp
    LIBS wiola_media
)

# dr_libs is a set of headers with no target of its own.
target_include_directories(
    wiola_codec_decode
    SYSTEM
    PRIVATE "${dr_libs_SOURCE_DIR}"
)
