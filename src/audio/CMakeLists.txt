include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/headers.cmake")

add_library(wiola_audio_output INTERFACE)

add_library(wiola::audio_output ALIAS wiola_audio_output)

target_compile_features(wiola_audio_output INTERFACE cxx_std_23)

wiola_target_headers(
    wiola_audio_output
    wiola/audio
    HEADERS source.hpp output.hpp output_connection.hpp stage.hpp
)

target_link_libraries(
    wiola_audio_output
    INTERFACE wiola::borrowed_reference wiola::media
)

add_library(
    wiola_audio_dsp
    STATIC
    processing/biquad.cpp
    processing/equalizer.cpp
    processing/equalizer_parameters.cpp
    processing/equalizer_processor.cpp
    processing/equalizer_publication.cpp
    processing/output_sanitizer.cpp
    processing/processing_chain.cpp
    processing/volume.cpp
)

add_library(wiola::audio_dsp ALIAS wiola_audio_dsp)

target_compile_features(wiola_audio_dsp PUBLIC cxx_std_23)

target_include_directories(
    wiola_audio_dsp
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)

wiola_target_headers(
    wiola_audio_dsp
    wiola/audio
    HEADERS
        processing/equalizer.hpp
        processing/equalizer_config.hpp
        processing/output_sanitizer.hpp
        processing/processing_chain.hpp
        processing/volume.hpp
        processing/volume_policy.hpp
)

target_link_libraries(wiola_audio_dsp PUBLIC wiola::audio_output)

if(COMMAND wiola_apply_build_controls)
    wiola_apply_build_controls(wiola_audio_dsp)
endif()

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# The complete audio owner includes its native adapter, while portable component builds do not
# prepare or compile a device SDK. The kernel still links only the contracts it consumes.
if(NOT WIOLA_COMPONENT_BUILD)
    add_subdirectory(miniaudio)

    wiola_add_module(
        wiola_audio
        LIBS wiola_audio_output wiola::audio_miniaudio wiola_audio_dsp
    )
endif()
