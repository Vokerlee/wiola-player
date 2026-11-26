include(headers)

add_library(wiola_codec_registry STATIC format.cpp registry.cpp)

add_library(wiola::codec_registry ALIAS wiola_codec_registry)

set_target_properties(wiola_codec_registry PROPERTIES CXX_EXTENSIONS OFF)

target_link_libraries(
    wiola_codec_registry
    PUBLIC wiola::media
    PRIVATE wiola::local_media
)

wiola_target_headers(
    wiola_codec_registry
    wiola/media/codecs
    HEADERS format.hpp registry.hpp
)

wiola_apply_build_controls(wiola_codec_registry)

add_subdirectory(ffmpeg)

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
    add_executable(wiola_ffmpeg_consumer tests/consumer.cpp)

    target_link_libraries(wiola_ffmpeg_consumer PRIVATE wiola::ffmpeg)

    wiola_apply_build_controls(wiola_ffmpeg_consumer)

    add_test(NAME ffmpeg_public_consumer COMMAND wiola_ffmpeg_consumer)
endif()

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
