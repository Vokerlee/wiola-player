add_subdirectory(device)

wiola_add_module(
    wiola_audio
    LIBS wiola_audio_output wiola_audio_device wiola_audio_dsp
)
