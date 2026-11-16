wiola_add_library(
    wiola_codec_decode
    SOURCES decoder.cpp format.cpp registry.cpp
    LIBS wiola_codec_dr_libs wiola_media
)

if(WIOLA_ENABLE_FFMPEG)
    target_link_libraries(wiola_codec_decode PRIVATE wiola_codec_ffmpeg)
    target_compile_definitions(wiola_codec_decode PRIVATE WIOLA_ENABLE_FFMPEG=1)
endif()
