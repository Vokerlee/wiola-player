wiola_add_library(wiola_local_media SOURCES file_stream.cpp resource.cpp)
add_library(wiola::local_media ALIAS wiola_local_media)
target_link_libraries(wiola_local_media PUBLIC wiola::media)

wiola_add_library(
    wiola_codec_provider
    SOURCES provider.cpp
    LIBS wiola_codec_decode wiola_codec_ffmpeg
)
if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
