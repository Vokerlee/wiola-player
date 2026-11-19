wiola_add_test(
    app_playback_service_test
    SOURCES playback_service_test.cpp
    LIBS wiola_app
)

if(WIOLA_BUILD_GUI)
    wiola_add_test(
        app_player_application_test
        SOURCES player_application_test.cpp
        LIBS wiola_player_application
    )
    wiola_add_test(
        app_queue_library_migration_test
        SOURCES queue_library_migration_test.cpp
        LIBS wiola_queue_migration
    )
endif()
