# The queue plays nothing, so it needs nothing to play with.
wiola_add_library(
    wiola_engine_queue
    SOURCES playback_order.cpp playlist.cpp track.cpp
)
target_link_libraries(wiola_engine_queue PUBLIC wiola::media)
