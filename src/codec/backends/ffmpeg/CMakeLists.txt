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
    LIBS wiola_local_file_stream wiola_ffmpeg
)
target_link_libraries(wiola_codec_ffmpeg PUBLIC wiola::media)
