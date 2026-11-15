add_subdirectory(decode)
add_subdirectory(tags)

wiola_add_library(
    wiola_codec_provider
    SOURCES provider.cpp
    LIBS wiola_codec_decode wiola_codec_tags
)
wiola_add_module(
    wiola_codec
    LIBS wiola_codec_decode wiola_codec_tags wiola_codec_provider
)
