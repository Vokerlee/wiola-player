// This translation unit deliberately has no Qt dependency.
#include <wiola/presentation/documents/queue_rows.hpp>
#include <wiola/presentation/documents/saved_playlist_rows.hpp>
#include <wiola/presentation/documents/session_documents.hpp>
#include <wiola/presentation/documents/workspace_rows.hpp>
#include <wiola/presentation/tracks/track_view_pipeline.hpp>
#if defined(QT_CORE_LIB) || defined(QT_GUI_LIB) || defined(QT_WIDGETS_LIB)
#error The presentation model must build without Qt.
#endif
int main()
{
    wiola::gui::SessionDocuments session;
    session.remap_queue({});
    if (session.open != std::vector{wiola::gui::DocumentId::queue()})
        return 1;
    return wiola::gui::apply_track_view({}, nullptr, {}, {}).empty() ? 0 : 1;
}
