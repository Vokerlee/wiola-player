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

#include "equalizer_panel.hpp"
#include "library_navigation.hpp"
#include "now_playing_document.hpp"
#include "player_actions.hpp"
#include "player_presenter.hpp"
#include "playlist_control.hpp"
#include "saved_playlist_document.hpp"
#include "saved_playlist_manager.hpp"
#include "saved_playlist_storage.hpp"
#include "seek_bar.hpp"
#include "track_inspector.hpp"
#include "volume_control.hpp"
#include "workspace_document.hpp"

#include <app/active_playlist.hpp>
#include <app/custom_tag_commands.hpp>
#include <app/m3u_playlist.hpp>
#include <app/media_revision_publisher.hpp>
#include <app/metadata_merge.hpp>
#include <app/optimistic_metadata_edits.hpp>
#include <app/playback_queue.hpp>
#include <app/playlist_transfer.hpp>
#include <app/workspace_watcher.hpp>
#include <codec/decode/open.hpp>
#include <engine/session.hpp>
#include <library/in_memory_repository.hpp>
#include <utils/units.hpp>

#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTabWidget>

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace wiola::gui {

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
    explicit MainWindow(engine::Session& session);

    ~MainWindow() override;

    /// Begins playing `tracks`, in the order given. Reading a file takes as long as it takes, so
    /// what is playing keeps playing and the window says what happened once it has been read.
    void open(std::vector<std::filesystem::path> tracks);

    /// Takes up the result of an application pump and reflects a changed track or playback fault.
    void update_after_pump();

    /// Reads the application-owned session and refreshes what is shown.
    void refresh();

private:
    /// Asks for files and plays them, in the order they were chosen.
    void choose_tracks();

    /// Asks a listener which files they mean, and answers with nothing when they say none.
    [[nodiscard]] std::vector<std::filesystem::path> ask_for_tracks();

    /// Asks for files and puts them at the end of the queue.
    void add_tracks();
    void import_m3u_playlist();
    void export_m3u_playlist();

    /// Takes whatever rows are picked out of the queue.
    void remove_chosen();

    /// Empties the queue.
    void clear_queue();

    /// Plays the track the listener picked out of the queue.
    void play_chosen(engine::QueueEntryId id);

    void open_playlist(const library::PlaylistId& id);
    void open_folder();
    void open_workspace(const library::WorkspaceId& id);
    void rescan_workspace(const library::WorkspaceId& id, bool recursive);
    void play_workspace_selection(WorkspaceDocument& document);
    void append_workspace_selection(WorkspaceDocument& document);
    void transfer_workspace(WorkspaceDocument& document, bool choose_destination);
    void show_workspace_navigation();
    void restore_workspace_documents();
    void save_workspace_documents();
    void activate_playlist(const library::PlaylistId& id);
    void transfer_now_playing(bool choose_destination);
    void transfer_saved_playlist(const library::PlaylistId& id, SavedPlaylistDocument& document,
        bool choose_destination);
    void choose_move_destination(const library::PlaylistId& id, SavedPlaylistDocument& document);
    void transfer_to(std::optional<library::PlaylistId> destination,
        const std::vector<app::MediaTransfer>& media);
    void choose_transfer_destination(const std::vector<app::MediaTransfer>& media);
    void play_saved_playlist(const library::PlaylistId& id);
    void play_displayed_paths(std::vector<std::filesystem::path> tracks);
    void request_metadata(const std::filesystem::path& location);
    void publish_media_revision(app::BackgroundMetadataResult result);
    void finish_custom_tag(app::CustomTagResult result, const QString& success);
    void finish_standard_metadata(library::MediaId media, library::StandardFieldOverrides edit);
    void publish_metadata_outcome(const app::MetadataEditOutcome& outcome);
    void resolve_metadata_conflict(MetadataConflictAction action);
    [[nodiscard]] std::vector<const library::TrackRecord*> selected_records();
    void refresh_playlist_documents();
    void restore_playlist_documents();
    void save_playlist_documents();

    /// Plays the track before or after this one in the list.
    void play_previous();
    void play_next();

    /// Steps between playing the list once, playing it round and round, and playing one track
    /// over: the three a listener cycles through with one button.
    void cycle_repeat();

    /// Plays the list in the order it was given, or in one drawn at random.
    void set_shuffled(bool shuffled);

    /// Connects a button to a shared action and copies its accessible presentation.
    void connect_action(QPushButton& button, PlayerAction id, bool icon_only = false);

    /// Shows the play button as pausing or as playing. Does nothing when it already shows that
    /// one: a window refreshing ten times a second would otherwise ask the style for an icon it
    /// is already wearing.
    void show_play_button(bool pausing);

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

    /// Moves playback to `fraction` of the loaded track.
    void seek_to(double fraction);

    /// Says `message`, or clears what was said when it is empty. Refreshing leaves this alone,
    /// so a message stays readable rather than lasting until the next poll.
    void show_status(const QString& message);

    [[nodiscard]] bool loaded() const noexcept { return presenter_.presentation().playback.loaded; }

    engine::Session& session_;
    PlayerPresenter presenter_;

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

    /// Which of the two it is wearing, or nothing before it wears either.
    std::optional<bool> showing_pause_;

    /// The file a run leaves things in.
    QSettings settings_;
    PlaylistControl playlist_;
    VolumeControl volume_;
    EqualizerControl equalizer_;
    library::InMemoryRepository library_;
    SavedPlaylistStorage playlist_storage_;
    app::SavedPlaylistCommands playlist_commands_;
    app::CustomTagCommands tag_commands_;
    app::OptimisticMetadataEdits optimistic_edits_;
    app::ActivePlaylist active_playlist_;

    struct PendingMetadataEdit {
        std::vector<library::MediaId> media;
        library::StandardFieldOverrides edit;
    };

    std::optional<PendingMetadataEdit> pending_metadata_edit_;

    QPushButton* open_button_{nullptr};
    QPushButton* previous_button_{nullptr};
    QPushButton* play_button_{nullptr};
    QPushButton* next_button_{nullptr};
    QPushButton* stop_button_{nullptr};
    QPushButton* repeat_button_{nullptr};
    QPushButton* shuffle_button_{nullptr};
    QPushButton* add_button_{nullptr};
    QPushButton* remove_button_{nullptr};
    QPushButton* clear_button_{nullptr};
    SeekBar* position_bar_{nullptr};
    NowPlayingDocument* now_playing_{nullptr};
    QTabWidget* documents_{nullptr};
    LibraryNavigation* navigation_{nullptr};
    SavedPlaylistManager* playlist_manager_{nullptr};
    std::map<library::PlaylistId, SavedPlaylistDocument*> playlist_documents_;
    std::map<library::WorkspaceId, WorkspaceDocument*> workspace_documents_;
    std::unique_ptr<app::WorkspaceScanPublisher> workspace_scans_;
    app::MetadataMerge metadata_merge_;
    std::unique_ptr<app::MediaRevisionPublisher> media_revisions_;
    std::unique_ptr<app::WorkspaceWatcher> workspace_watcher_;
    TrackInspector* inspector_{nullptr};
    QSlider* volume_slider_{nullptr};
    QLabel* volume_value_{nullptr};
    QPushButton* equalizer_button_{nullptr};
    QPushButton* boost_button_{nullptr};
    EqualizerPanel* equalizer_panel_{nullptr};
    QLabel* track_label_{nullptr};
    QLabel* elapsed_label_{nullptr};
    QLabel* total_label_{nullptr};
    QLabel* status_label_{nullptr};
};

} // namespace wiola::gui
