add_subdirectory(ffmpeg)

wiola_add_library(
    wiola_codec_decode
    SOURCES format.cpp registry.cpp
    LIBS wiola::local_media
)

target_link_libraries(wiola_codec_decode PUBLIC wiola::media)

wiola_add_module(wiola_codec LIBS wiola_codec_decode wiola_ffmpeg_provider)

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
