wiola_add_library(
    wiola_codec_decode
    SOURCES format.cpp registry.cpp
    LIBS wiola_codec_ffmpeg wiola_local_file_stream
)
target_link_libraries(wiola_codec_decode PUBLIC wiola::media)
