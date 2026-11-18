wiola_add_library(wiola_app SOURCES playback_service.cpp LIBS wiola_engine)

if(WIOLA_BUILD_GUI)
    wiola_add_library(
        wiola_player_application
        SOURCES player_application.cpp
        LIBS wiola_gui
    )
    set_target_properties(wiola_player_application PROPERTIES AUTOMOC ON)
    target_link_libraries(wiola_player_application PUBLIC Qt6::Widgets)
endif()
