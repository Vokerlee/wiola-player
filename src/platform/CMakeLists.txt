if(NOT WIOLA_BUILD_GUI)
    return()
endif()
add_library(wiola_qt_platform INTERFACE)
target_link_libraries(wiola_qt_platform INTERFACE wiola_core Qt6::Core)
wiola_add_library(
    wiola_qt_settings
    SOURCES qt/player_settings.cpp
    LIBS wiola_qt_platform
)
target_link_libraries(wiola_qt_settings PUBLIC Qt6::Core wiola::audio_dsp)
