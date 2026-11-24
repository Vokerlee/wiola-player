wiola_add_test(audio_biquad_test SOURCES biquad_test.cpp LIBS wiola_audio_dsp)
wiola_add_test(
    audio_output_sanitizer_test
    SOURCES output_sanitizer_test.cpp
    LIBS wiola_audio_dsp
)
wiola_add_test(audio_volume_test SOURCES volume_test.cpp LIBS wiola_audio_dsp)
wiola_add_test(
    audio_equalizer_test
    SOURCES equalizer_test.cpp
    LIBS wiola_audio_dsp
)
wiola_add_test(
    audio_processing_chain_test
    SOURCES processing_chain_test.cpp
    LIBS wiola_audio_dsp
)
wiola_add_test(audio_relay_test SOURCES relay_test.cpp LIBS wiola_audio_dsp)
wiola_add_test(
    audio_shaped_source_test
    SOURCES shaped_source_test.cpp
    LIBS wiola_audio_dsp
)
wiola_add_test(audio_tone_test SOURCES tone_test.cpp LIBS wiola_audio_dsp)
wiola_add_test(audio_device_test SOURCES device_test.cpp LIBS wiola_audio)
