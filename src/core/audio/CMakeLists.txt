include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(audio_public "${CMAKE_CURRENT_SOURCE_DIR}/public")
set(audio_package "${CMAKE_INSTALL_LIBDIR}/cmake/wiola_audio")

add_library(wiola_audio_output INTERFACE)
add_library(wiola::audio_output ALIAS wiola_audio_output)
set_target_properties(wiola_audio_output PROPERTIES EXPORT_NAME audio_output)
target_compile_features(wiola_audio_output INTERFACE cxx_std_23)
target_include_directories(
    wiola_audio_output
    INTERFACE
        $<BUILD_INTERFACE:${audio_public}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_sources(
    wiola_audio_output
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS "${audio_public}"
            FILES
                public/wiola/audio/source.hpp
                public/wiola/audio/output.hpp
                public/wiola/audio/output_connection.hpp
)
target_link_libraries(
    wiola_audio_output
    INTERFACE wiola::borrowed_reference wiola::media
)

add_library(
    wiola_audio_dsp
    STATIC
    private/processing/biquad.cpp
    private/processing/equalizer.cpp
    private/processing/equalizer_parameters.cpp
    private/processing/equalizer_processor.cpp
    private/processing/output_sanitizer.cpp
    private/processing/processing_chain.cpp
    private/processing/relay.cpp
    private/processing/shaped_source.cpp
    private/processing/volume.cpp
)
add_library(wiola::audio_dsp ALIAS wiola_audio_dsp)
set_target_properties(wiola_audio_dsp PROPERTIES EXPORT_NAME audio_dsp)
target_compile_features(wiola_audio_dsp PUBLIC cxx_std_23)
target_include_directories(
    wiola_audio_dsp
    PUBLIC
        $<BUILD_INTERFACE:${audio_public}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE private
)
target_sources(
    wiola_audio_dsp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS "${audio_public}"
            FILES
                public/wiola/audio/stage.hpp
                public/wiola/audio/equalizer.hpp
                public/wiola/audio/equalizer_config.hpp
                public/wiola/audio/output_sanitizer.hpp
                public/wiola/audio/processing_chain.hpp
                public/wiola/audio/relay.hpp
                public/wiola/audio/shaped_source.hpp
                public/wiola/audio/volume.hpp
                public/wiola/audio/volume_policy.hpp
)
target_link_libraries(wiola_audio_dsp PUBLIC wiola::audio_output)
if(COMMAND wiola_apply_build_controls)
    wiola_apply_build_controls(wiola_audio_dsp)
endif()

# Output-only installations contain neither the processing archive nor its headers.
install(
    TARGETS wiola_audio_output
    EXPORT wiola_audio_targets
    FILE_SET HEADERS
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT wiola_audio
)
install(
    EXPORT wiola_audio_targets
    FILE wiola_audio-targets.cmake
    NAMESPACE wiola::
    DESTINATION ${audio_package}
    COMPONENT wiola_audio
)
configure_package_config_file(
    package.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/wiola_audio-config.cmake"
    INSTALL_DESTINATION ${audio_package}
    NO_SET_AND_CHECK_MACRO
)
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/wiola_audio-config.cmake"
    DESTINATION ${audio_package}
    COMPONENT wiola_audio
)
install(
    TARGETS wiola_audio_dsp
    EXPORT wiola_audio_dsp_targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT wiola_audio_dsp
    FILE_SET HEADERS
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT wiola_audio_dsp
)
install(
    EXPORT wiola_audio_dsp_targets
    FILE wiola_audio-dsp-targets.cmake
    NAMESPACE wiola::
    DESTINATION ${audio_package}
    COMPONENT wiola_audio_dsp
)

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
