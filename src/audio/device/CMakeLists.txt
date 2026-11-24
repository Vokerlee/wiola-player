wiola_add_library(
    wiola_audio_device
    SOURCES device.cpp
    LIBS miniaudio wiola_audio_output wiola_audio_dsp
)
target_link_libraries(wiola_audio_device PUBLIC wiola::audio_output)
