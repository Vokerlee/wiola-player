#include <qt/application/session/session_preferences.hpp>
#include <qt/projection/track_projection.hpp>
#include <qt/storage/library_store.hpp>
#if defined(QT_GUI_LIB) || defined(QT_WIDGETS_LIB)
#error Core adapters must not depend on Qt Gui or Widgets.
#endif
int main()
{
    return wiola::gui::TrackProjectionInput{}.rows.empty() ? 0 : 1;
}
