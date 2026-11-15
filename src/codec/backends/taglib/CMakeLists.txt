wiola_add_library(wiola_codec_tags SOURCES tags.cpp LIBS tag wiola_media)

target_include_directories(
    wiola_codec_tags
    SYSTEM
    PRIVATE
        "${taglib_SOURCE_DIR}"
        "${taglib_SOURCE_DIR}/taglib"
        "${taglib_BINARY_DIR}"
)
