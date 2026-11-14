wiola_add_library(
    wiola_audio_device
    SOURCES device.cpp output_connection.cpp
    LIBS miniaudio wiola_audio_dsp
)
