# Shaping consumes the source protocol without linking a device backend.
wiola_add_library(
    wiola_audio_dsp
    SOURCES
        biquad.cpp
        buffer_source.cpp
        equalizer.cpp
        equalizer_parameters.cpp
        equalizer_processor.cpp
        output_sanitizer.cpp
        processing_chain.cpp
        relay.cpp
        shaped_source.cpp
        volume.cpp
)
target_link_libraries(wiola_audio_dsp PUBLIC wiola_audio_output)
