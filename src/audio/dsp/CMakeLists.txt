# Shaping needs no audio stack, and links nothing.
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
