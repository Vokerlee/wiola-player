// This translation unit deliberately has no Qt dependency.
#include <presentation/documents/queue_rows.hpp>
#include <presentation/documents/saved_playlist_rows.hpp>
#include <presentation/documents/workspace_rows.hpp>
#include <presentation/tracks/track_view_pipeline.hpp>
#if defined(QT_CORE_LIB) || defined(QT_GUI_LIB) || defined(QT_WIDGETS_LIB)
#error The presentation model must build without Qt.
#endif
int main()
{
    return wiola::gui::apply_track_view({}, nullptr, {}, {}).empty() ? 0 : 1;
}
