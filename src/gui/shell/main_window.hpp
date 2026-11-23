/**
 * @file
 * @brief The window a listener drives one track from.
 * @author Roman Glaz
 * @copyright (c) 2026, <vokerlee@gmail.com>
 *
 * Wiola is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Wiola is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Wiola. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

// moc only needs the Qt declaration; the collaborators include C++23 concepts it cannot parse.
#ifndef Q_MOC_RUN
#include "player_actions.hpp"
#include <engine/open_result.hpp>
#include <gui/layout/layout_session.hpp>
#include <gui/layout/view_registry.hpp>
#include <library/library_changes.hpp>
#endif

#include <QMainWindow>
#include <QPointer>
#include <QSettings>

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class QSplitter;
class QTabWidget;
class QCloseEvent;
class QActionGroup;

namespace wiola::app {

class PlaybackService;
struct PlayerPresentation;
class PlaybackPreferences;
class LibrarySession;
class SessionPreferences;
class QueueSettings;
class VolumeControl;
class EqualizerControl;
class ActivePlaylist;

} // namespace wiola::app

namespace wiola::library {

class LibraryRepository;

}

namespace wiola::gui {

class PlayerDialogs;
class ThemeManager;
class TransportView;
class VolumeView;
class StatusView;
class NowPlayingDocument;
class LibraryNavigation;
class SavedPlaylistManager;
class DocumentHost;
class TrackInspector;
class EqualizerPanel;
class DocumentCommandRouter;
class MetadataInspectorBinding;

/**
 * Shows one track and the controls for it.
 *
 * The application pumps its session independently. The window routes commands through its
 * presenter and reads that state when asked, so replacing widgets does not replace playback.
 *
 * A window opens with nothing loaded and is given a track later, so that choosing one is an
 * ordinary thing to do rather than something only the command line can arrange.
 */
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(app::PlaybackService& playback, app::LibrarySession& library,
        app::PlaybackPreferences& preferences);

    ~MainWindow() override;

    /// Begins playing `tracks`, in the order given. Reading a file takes as long as it takes, so
    /// what is playing keeps playing and the window says what happened once it has been read.
    void open(std::vector<std::filesystem::path> tracks);

    /// Takes up the result of an application pump and reflects a changed track or playback fault.
    void update_after_pump(const app::PlayerPresentation& presentation);

    /// Reads the application-owned session and refreshes what is shown.
    void refresh();

    /// Builds a complete layout before replacing the current one, retaining registered widgets.
    [[nodiscard]] bool switch_layout(const LayoutDocument& document);
    [[nodiscard]] bool import_layout(QByteArrayView bytes);
    [[nodiscard]] QByteArray layout_json() const;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void save_window_state();
    void show_layout_choice();
    bool install_layout(const LayoutDocument& document, LayoutChoice choice, bool persist);
    void show_workspace_navigation();
    void apply_library_changes(const library::LibraryChangeBatch& batch);
    /// Plays the track before or after this one in the list.
    void play_previous();
    void play_next();

    /// Steps between playing the list once, playing it round and round, and playing one track
    /// over: the three a listener cycles through with one button.
    void cycle_repeat();

    /// Plays the list in the order it was given, or in one drawn at random.
    void set_shuffled(bool shuffled);

    /// Says which of the three repeats is on, and whether the order is shuffled. Whatever changes
    /// either must call this: refreshing no longer asks.
    void show_list_buttons();

    /// Starts playing, or resumes after a pause. The first press is what opens the device.
    void toggle_playback();

    void stop_playback();

    /// Opens the equalizer, building it the first time it is asked for.
    void show_equalizer();

    /// Sets how loud the output is, from a slider position out of a hundred, and keeps it for the
    /// runs after this one.
    void set_volume(int percent);

    /// Lets the slider ask for more than arrived, or stops it doing so.
    void set_boosted(bool boosted);

    /// Says how loud the output is, as the slider has it.
    void show_volume();
    void show_document_context();

    /// Moves playback to `fraction` of the loaded track.
    void seek_to(double fraction);

    /// Says `message`, or clears what was said when it is empty. Refreshing leaves this alone,
    /// so a message stays readable rather than lasting until the next poll.
    void show_status(const QString& message);

    [[nodiscard]] bool loaded() const noexcept;

    app::PlaybackService& playback_;

    /// What the window has already said about a load, so that it says it once.
    engine::OpenResult said_{engine::OpenResult::opened};

    /// What it has already said about playback going wrong, for the same reason.
    bool said_fault_{false};

    /// The track the title names, and where in the queue it stood: a listener may queue the
    /// same file twice, and moving from one of them to the other is still a new track.
    std::filesystem::path named_;
    std::optional<std::size_t> named_place_;

    /// Shared command objects; buttons and keyboard shortcuts trigger the same actions.
    PlayerActions actions_;
    ViewRegistry views_;
    std::unique_ptr<LayoutSession> layout_session_;
    QActionGroup* layout_presets_{nullptr};

    /// The file a run leaves things in.
    QSettings settings_;
    std::shared_ptr<ThemeManager> theme_;
    app::SessionPreferences& ui_preferences_;
    app::QueueSettings& playlist_;
    app::VolumeControl& volume_;
    app::EqualizerControl& equalizer_;
    app::LibrarySession& library_session_;
    const library::LibraryRepository& library_;
    library::LibraryChanges::Subscription library_changes_;
    const app::ActivePlaylist& active_playlist_;

    std::unique_ptr<PlayerDialogs> dialogs_;
    DocumentCommandRouter* commands_{nullptr};
    QPointer<MetadataInspectorBinding> inspector_binding_;
    TransportView* transport_{nullptr};
    VolumeView* volume_view_{nullptr};
    StatusView* status_{nullptr};
    NowPlayingDocument* now_playing_{nullptr};
    QTabWidget* documents_{nullptr};
    QWidget* library_pane_{nullptr};
    QSplitter* document_area_{nullptr};
    LibraryNavigation* navigation_{nullptr};
    SavedPlaylistManager* playlist_manager_{nullptr};
    DocumentHost* document_host_{nullptr};
    TrackInspector* inspector_{nullptr};
    EqualizerPanel* equalizer_panel_{nullptr};
};

} // namespace wiola::gui
