add_subdirectory(backends)
add_subdirectory(decode)

wiola_add_library(
    wiola_codec_provider
    SOURCES provider.cpp
    LIBS wiola_codec_decode wiola_codec_tags
)
if(WIOLA_ENABLE_FFMPEG)
    target_link_libraries(wiola_codec_provider PRIVATE wiola_codec_ffmpeg)
    target_compile_definitions(
        wiola_codec_provider
        PRIVATE WIOLA_ENABLE_FFMPEG=1
    )
endif()

wiola_add_module(
    wiola_codec
    LIBS wiola_codec_decode wiola_codec_tags wiola_codec_provider
)
