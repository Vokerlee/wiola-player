wiola_add_library(
    wiola_codec_ffmpeg
    SOURCES
        decoder.cpp
        input.cpp
        metadata.cpp
        metadata_writer.cpp
        packet_decoder.cpp
        pcm_converter.cpp
        qualification.cpp
    LIBS wiola::local_media wiola_ffmpeg
)

target_link_libraries(wiola_codec_ffmpeg PUBLIC wiola::media)

wiola_add_library(
    wiola_ffmpeg_provider
    SOURCES composition.cpp
    LIBS wiola_codec_provider
)
