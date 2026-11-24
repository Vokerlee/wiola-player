wiola_add_library(wiola_local_file_stream SOURCES file_stream.cpp)
target_link_libraries(wiola_local_file_stream PUBLIC wiola::media)

wiola_add_library(
    wiola_codec_provider
    SOURCES provider.cpp
    LIBS wiola_codec_decode wiola_codec_ffmpeg
)
if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
