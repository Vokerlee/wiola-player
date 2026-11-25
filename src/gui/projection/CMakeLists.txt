wiola_add_library(
    wiola_gui_projection
    SOURCES track_projection.cpp
    LIBS wiola::presentation wiola_desktop_delivery
)
target_link_libraries(wiola_gui_projection PUBLIC Qt6::Core wiola::presentation)
add_executable(wiola_core_adapter_boundary EXCLUDE_FROM_ALL boundary.cpp)
target_link_libraries(
    wiola_core_adapter_boundary
    PRIVATE
        wiola_gui_projection
        wiola_session_preferences
        wiola::qt_library_store
        wiola_sanitizer
        wiola_coverage
)
