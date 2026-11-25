add_library(wiola_file_fingerprint STATIC fingerprint.cpp)

add_library(wiola::file_fingerprint ALIAS wiola_file_fingerprint)

target_compile_features(wiola_file_fingerprint PUBLIC cxx_std_23)

target_include_directories(
    wiola_file_fingerprint
    PUBLIC "${PROJECT_SOURCE_DIR}/src"
)

wiola_apply_build_controls(wiola_file_fingerprint)

wiola_add_library(wiola_file_replacement SOURCES file_replacement.cpp)

add_library(wiola::file_replacement ALIAS wiola_file_replacement)

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
