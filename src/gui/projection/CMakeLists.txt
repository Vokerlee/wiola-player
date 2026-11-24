wiola_add_library(
    wiola_gui_projection
    SOURCES track_projection.cpp
    LIBS wiola_gui_model wiola_desktop_delivery
)
target_link_libraries(wiola_gui_projection PUBLIC Qt6::Core wiola_gui_model)
add_executable(wiola_core_adapter_boundary EXCLUDE_FROM_ALL boundary.cpp)
target_link_libraries(
    wiola_core_adapter_boundary
    PRIVATE
        wiola_gui_projection
        wiola_session_preferences
        wiola_persistence_qt
        wiola_sanitizer
        wiola_coverage
)
