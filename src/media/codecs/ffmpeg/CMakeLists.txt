include(headers)

wiola_add_library(
    wiola_codec_ffmpeg
    SOURCES
        decoder.cpp
        input.cpp
        metadata.cpp
        metadata_text.cpp
        metadata_writer.cpp
        packet_decoder.cpp
        pcm_converter.cpp
        qualification.cpp
    LIBS wiola::local_media wiola_ffmpeg wiola::codec_registry
)

target_link_libraries(wiola_codec_ffmpeg PUBLIC wiola::media)

add_library(wiola_ffmpeg_provider STATIC composition.cpp provider.cpp)

add_library(wiola::ffmpeg ALIAS wiola_ffmpeg_provider)

set_target_properties(wiola_ffmpeg_provider PROPERTIES CXX_EXTENSIONS OFF)

target_link_libraries(
    wiola_ffmpeg_provider
    PUBLIC wiola::media wiola::codec_registry
    PRIVATE wiola_codec_ffmpeg wiola::local_media
)

wiola_target_headers(
    wiola_ffmpeg_provider
    wiola/media/codecs/ffmpeg
    HEADERS composition.hpp
)

wiola_apply_build_controls(wiola_ffmpeg_provider)
