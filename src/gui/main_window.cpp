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

#include "main_window.hpp"

#include "clock.hpp"
#include "player_settings.hpp"
#include "presentation_tokens.hpp"
#include "qt_path_adapter.hpp"

#include <codec/provider.hpp>
#include <library/location.hpp>
#include <library/metadata.hpp>

#include <QCursor>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QSaveFile>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <optional>

namespace wiola::gui {

namespace {

constexpr PresentationTokens tokens;

QWidget* placeholder_document(QWidget* parent, const QString& description)
{
    QWidget* document = new QWidget{parent};
    QLabel* label = new QLabel{description, document};
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);

    QVBoxLayout* layout = new QVBoxLayout{document};
    layout->addWidget(label);

    return document;
}

std::filesystem::path library_file(const QSettings& settings)
{
    return QtPathAdapter::from_qt(QFileInfo{settings.fileName()}.absolutePath() + "/library.json");
}

/// What to say about a file that would not open.
QString as_status(engine::OpenResult result)
{
    switch (result) {
    case engine::OpenResult::unsupported:
        return QString{"unsupported format"};

    case engine::OpenResult::damaged:
        return QString{"that file is damaged"};

    case engine::OpenResult::unreadable:
        return QString{"cannot read that file"};

    default:
        return QString{"cannot read that file"};
    }
}

} // namespace

MainWindow::MainWindow(engine::Session& session)
    // A file of its own on both platforms, rather than the registry on one of them.
    : session_{session}
    , presenter_{session}
    , actions_{*this}
    , settings_{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"}
    , playlist_{presenter_, settings_}
    , volume_{presenter_.volume_control(), settings_}
    , equalizer_{presenter_.equalizer_control(), settings_}
    , playlist_storage_{library_file(settings_)}
    , playlist_commands_{library_,
          [this](const library::LibraryRepository& records) {
              return playlist_storage_.save(records) == LibraryJsonError::none;
          }}
    , tag_commands_{library_,
          [this](const library::LibraryRepository& records) {
              return playlist_storage_.save(records) == LibraryJsonError::none;
          }}
    , optimistic_edits_{library_,
          [this](const library::LibraryRepository& records) {
              return playlist_storage_.save(records) == LibraryJsonError::none;
          },
          [this](const app::MetadataEditOutcome& outcome) { publish_metadata_outcome(outcome); }}
{
    setWindowTitle("Wiola Player");
    setMinimumSize(tokens.player.window_min_width, tokens.player.window_min_height);

    auto* central = new QWidget{this};
    setCentralWidget(central);

    // Every widget is given this window as its parent, so Qt owns them and destroys them with it.
    open_button_ = new QPushButton{this};
    previous_button_ = new QPushButton{this};
    play_button_ = new QPushButton{this};
    next_button_ = new QPushButton{this};
    stop_button_ = new QPushButton{this};

    connect_action(*open_button_, PlayerAction::open, true);
    connect_action(*previous_button_, PlayerAction::previous, true);
    connect_action(*play_button_, PlayerAction::play, true);
    connect_action(*next_button_, PlayerAction::next, true);
    connect_action(*stop_button_, PlayerAction::stop, true);

    show_play_button(false);

    play_button_->setFixedWidth(tokens.player.play_button_width);
    repeat_button_ = new QPushButton{this};
    shuffle_button_ = new QPushButton{this};
    add_button_ = new QPushButton{this};
    remove_button_ = new QPushButton{this};
    clear_button_ = new QPushButton{this};

    connect_action(*repeat_button_, PlayerAction::repeat);
    connect_action(*shuffle_button_, PlayerAction::shuffle);
    connect_action(*add_button_, PlayerAction::add);
    connect_action(*remove_button_, PlayerAction::remove);
    connect_action(*clear_button_, PlayerAction::clear);

    equalizer_button_ = new QPushButton{this};
    connect_action(*equalizer_button_, PlayerAction::equalizer);
    equalizer_button_->setAccessibleName("Equalizer");
    equalizer_button_->setToolTip("Open equalizer");

    boost_button_ = new QPushButton{"+", this};
    volume_value_ = new QLabel{this};
    position_bar_ = new SeekBar{this};
    now_playing_ = new NowPlayingDocument{this};
    track_label_ = new QLabel{this};
    elapsed_label_ = new QLabel{this};
    total_label_ = new QLabel{this};
    status_label_ = new QLabel{this};
    volume_slider_ = new QSlider{Qt::Horizontal, this};

    if (playlist_storage_.restore(library_) != LibraryJsonError::none)
        show_status("Could not restore the saved library");

    const library::PlaylistId active{settings_.value("playlists/active").toString().toStdString()};
    if (library_.playlist(active) != nullptr)
        static_cast<void>(active_playlist_.set(active));

    const PlayerSettings restored{QtSettingsAdapter{settings_}.read(volume_.scale(),
        presenter_.equalizer_control().config())};
    volume_.apply_restored(restored.volume_position, restored.boosted);
    equalizer_.apply_restored(restored.equalizer_enabled, restored.band_gains);
    const int position{volume_.position()};

    boost_button_->setCheckable(true);
    boost_button_->setChecked(volume_.boosted());
    boost_button_->setFixedWidth(tokens.player.boost_button_width);
    boost_button_->setFocusPolicy(Qt::StrongFocus);
    boost_button_->setAccessibleName("Boost volume above 100%");
    boost_button_->setToolTip("Allow volume above 100%");

    volume_value_->setFixedWidth(tokens.player.volume_value_width);

    volume_slider_->setRange(0, volume_.max_position());
    volume_slider_->setValue(position);

    show_volume();
    volume_slider_->setFixedWidth(tokens.player.volume_slider_width);
    volume_slider_->setToolTip("Volume");

    QFont title{track_label_->font()};
    title.setPointSize(title.pointSize() + tokens.player.track_name_point_size_step);
    title.setBold(true);
    track_label_->setFont(title);
    track_label_->setAccessibleName("Current track");
    elapsed_label_->setAccessibleName("Elapsed time");
    total_label_->setAccessibleName("Total duration");
    status_label_->setAccessibleName("Player status");

    total_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // What is playing: its name, where it has reached, and how far that is through it.
    auto* times = new QHBoxLayout;
    times->addWidget(elapsed_label_);
    times->addStretch();
    times->addWidget(total_label_);

    // How it plays. Everything here acts on the track that is on.
    auto* transport = new QHBoxLayout;
    transport->setSpacing(tokens.player.control_spacing);
    transport->addWidget(previous_button_);
    transport->addWidget(play_button_);
    transport->addWidget(next_button_);
    transport->addWidget(stop_button_);
    transport->addStretch();
    transport->addWidget(volume_slider_);
    transport->addWidget(volume_value_);
    transport->addWidget(boost_button_);

    QGroupBox* playback_group = new QGroupBox{"Now playing", this};
    playback_group->setObjectName("playbackGroup");
    QVBoxLayout* playback_layout = new QVBoxLayout{playback_group};
    playback_layout->addWidget(track_label_);
    playback_layout->addWidget(position_bar_);
    playback_layout->addLayout(times);
    playback_layout->addLayout(transport);

    // What the list does. Repeat and shuffle change what plays next rather than how this one
    // plays, which is why they are not up there with the transport.
    auto* list = new QHBoxLayout;
    list->setSpacing(tokens.player.control_spacing);
    list->addWidget(open_button_);
    list->addWidget(repeat_button_);
    list->addWidget(shuffle_button_);
    list->addStretch();
    list->addWidget(equalizer_button_);

    auto* layout = new QVBoxLayout{central};
    layout->setSpacing(tokens.player.section_spacing);
    layout->addWidget(playback_group);
    layout->addLayout(list);

    documents_ = new QTabWidget{this};
    documents_->setObjectName("documentTabs");
    documents_->setDocumentMode(true);
    documents_->addTab(now_playing_, "Now Playing");
    documents_->addTab(placeholder_document(documents_,
                           "Folder workspaces will open here without changing the playback queue."),
        "Folder Workspace");

    navigation_ = new LibraryNavigation{this};
    navigation_->show_repository(library_);
    show_workspace_navigation();
    playlist_manager_ = new SavedPlaylistManager{library_, playlist_commands_, *navigation_, this};
    inspector_ = new TrackInspector{this};

    inspector_->custom_tags()->on_add([this](library::MediaId media, std::string key,
                                          std::string value) {
        finish_custom_tag(tag_commands_.add(media, std::move(key), std::move(value)),
            "Custom tag added");
    });
    inspector_->custom_tags()->on_edit([this](library::MediaId media, library::TagId tag,
                                           std::string key, std::string value) {
        finish_custom_tag(tag_commands_.edit(media, tag, std::move(key), std::move(value)),
            "Custom tag updated");
    });
    inspector_->custom_tags()->on_remove([this](library::MediaId media, library::TagId tag) {
        finish_custom_tag(tag_commands_.remove(media, tag), "Custom tag removed");
    });
    inspector_->standard_metadata()->on_apply([this](library::MediaId media,
                                                  library::StandardFieldOverrides edit) {
        finish_standard_metadata(media, std::move(edit));
    });
    inspector_->conflicts()->on_action([this](MetadataConflictAction action) {
        resolve_metadata_conflict(action);
    });

    workspace_scans_ =
        std::make_unique<app::WorkspaceScanPublisher>(*this, [this](app::WorkspaceScanBatch batch) {
            const auto found = workspace_documents_.find(batch.workspace);
            if (found == workspace_documents_.end() || !found->second->show_batch(batch))
                return;

            for (const std::filesystem::path& location : batch.tracks)
                request_metadata(location);
        });

    media_revisions_ = std::make_unique<app::MediaRevisionPublisher>(
        *this, std::make_shared<codec::LocalMediaProvider>(),
        [this](app::BackgroundMetadataResult result) { publish_media_revision(std::move(result)); }
    );

    for (const library::TrackRecord& track : library_.tracks()) {
        if (!track.locations.empty())
            request_metadata(track.locations.front());
    }

    workspace_watcher_ = std::make_unique<app::WorkspaceWatcher>(this);
    workspace_watcher_->on_changed([this](library::WorkspaceId id) {
        if (const library::WorkspaceRecord* workspace = library_.workspace(id))
            rescan_workspace(id, workspace->recursive);
    });

    for (const library::WorkspaceRecord& workspace : library_.workspaces())
        static_cast<void>(workspace_watcher_->watch(workspace));

    QWidget* library_pane = new QWidget{this};
    QVBoxLayout* library_layout = new QVBoxLayout{library_pane};
    library_layout->setContentsMargins(0, 0, 0, 0);
    library_layout->addWidget(navigation_);
    library_layout->addWidget(playlist_manager_);

    QSplitter* document_area = new QSplitter{this};
    document_area->setObjectName("documentArea");
    document_area->addWidget(library_pane);
    document_area->addWidget(documents_);
    document_area->addWidget(inspector_);
    document_area->setStretchFactor(0, 0);
    document_area->setStretchFactor(1, 1);
    document_area->setStretchFactor(2, 0);
    document_area->setSizes({180, 620, 220});
    layout->addWidget(document_area);

    navigation_->on_playlist_chosen([this](library::PlaylistId id) { open_playlist(id); });
    navigation_->on_workspace_chosen([this](library::WorkspaceId id) { open_workspace(id); });
    playlist_manager_->on_changed([this] { refresh_playlist_documents(); });
    connect(documents_, &QTabWidget::currentChanged, this, [this](int) {
        save_playlist_documents();
        save_workspace_documents();
    });

    restore_playlist_documents();
    restore_workspace_documents();

    // What is done to the queue, under the queue it is done to.
    QGroupBox* queue_group = new QGroupBox{"Queue commands", this};
    queue_group->setObjectName("queueCommandsGroup");
    auto* queue = new QHBoxLayout{queue_group};
    queue->setSpacing(tokens.player.control_spacing);
    queue->addWidget(add_button_);
    queue->addWidget(remove_button_);
    queue->addWidget(clear_button_);
    queue->addStretch();

    layout->addWidget(queue_group);

    // Kept in the layout while it says nothing, so a message appearing does not resize the window.
    layout->addWidget(status_label_);

    connect(actions_.action(PlayerAction::open), &QAction::triggered, this,
        &MainWindow::choose_tracks);
    connect(actions_.action(PlayerAction::previous), &QAction::triggered, this,
        &MainWindow::play_previous);
    connect(actions_.action(PlayerAction::next), &QAction::triggered, this, &MainWindow::play_next);
    connect(actions_.action(PlayerAction::play), &QAction::triggered, this,
        &MainWindow::toggle_playback);
    connect(actions_.action(PlayerAction::stop), &QAction::triggered, this,
        &MainWindow::stop_playback);
    connect(actions_.action(PlayerAction::repeat), &QAction::triggered, this,
        &MainWindow::cycle_repeat);
    connect(actions_.action(PlayerAction::shuffle), &QAction::triggered, this,
        &MainWindow::set_shuffled);
    connect(actions_.action(PlayerAction::add), &QAction::triggered, this, &MainWindow::add_tracks);
    connect(actions_.action(PlayerAction::remove), &QAction::triggered, this,
        &MainWindow::remove_chosen);
    connect(actions_.action(PlayerAction::clear), &QAction::triggered, this,
        &MainWindow::clear_queue);
    connect(actions_.action(PlayerAction::equalizer), &QAction::triggered, this,
        &MainWindow::show_equalizer);

    QMenu* file = menuBar()->addMenu("&File");
    file->addAction(actions_.action(PlayerAction::open));
    file->addAction(actions_.action(PlayerAction::add));
    file->addAction(actions_.action(PlayerAction::clear));
    file->addSeparator();

    QAction* open_folder = file->addAction("Open folder...");
    connect(open_folder, &QAction::triggered, this, &MainWindow::open_folder);
    file->addSeparator();

    QAction* import_m3u = file->addAction("Import M3U8...");
    QAction* export_m3u = file->addAction("Export M3U8...");
    connect(import_m3u, &QAction::triggered, this, &MainWindow::import_m3u_playlist);
    connect(export_m3u, &QAction::triggered, this, &MainWindow::export_m3u_playlist);

    QMenu* playback = menuBar()->addMenu("&Playback");
    playback->addAction(actions_.action(PlayerAction::previous));
    playback->addAction(actions_.action(PlayerAction::play));
    playback->addAction(actions_.action(PlayerAction::next));
    playback->addAction(actions_.action(PlayerAction::stop));
    playback->addSeparator();
    playback->addAction(actions_.action(PlayerAction::repeat));
    playback->addAction(actions_.action(PlayerAction::shuffle));

    QMenu* tools = menuBar()->addMenu("&Tools");
    tools->addAction(actions_.action(PlayerAction::equalizer));

    connect(now_playing_, &NowPlayingDocument::track_chosen, this, &MainWindow::play_chosen);
    connect(now_playing_, &NowPlayingDocument::removal_asked, this, &MainWindow::remove_chosen);
    connect(now_playing_, &NowPlayingDocument::transfer_active_asked, this,
        [this] { transfer_now_playing(false); });
    connect(now_playing_, &NowPlayingDocument::transfer_destination_asked, this,
        [this] { transfer_now_playing(true); });
    connect(now_playing_, &NowPlayingDocument::order_application_asked, this,
        [this] { play_displayed_paths(now_playing_->displayed_paths(session_.playlist())); });

    connect(volume_slider_, &QSlider::valueChanged, this, &MainWindow::set_volume);
    connect(boost_button_, &QPushButton::toggled, this, &MainWindow::set_boosted);

    connect(position_bar_, &SeekBar::seek_requested, this, &MainWindow::seek_to);

    // The clock follows the drag rather than waiting for the next poll.
    connect(position_bar_, &SeekBar::dragged, this, &MainWindow::refresh);

    if (const std::size_t gone{playlist_.restore()}; gone != 0)
        show_status(QString{"%1 queued track(s) are no longer where they were"}.arg(gone));

    // What was put back settles both of these, without either button having been pressed.
    show_list_buttons();

    refresh();
}

MainWindow::~MainWindow() = default;

void MainWindow::open(std::vector<std::filesystem::path> tracks)
{
    if (tracks.empty())
        return;

    const QString first{QtPathAdapter::to_qt(tracks.front().filename())};

    for (const std::filesystem::path& location : tracks)
        request_metadata(location);

    static_cast<void>(presenter_.open(std::move(tracks)));
    said_ = presenter_.presentation().playback.open_result;

    playlist_.save();
    show_status(QString{"opening "} + first + "...");

    // A new list is an unshuffled one, whether or not the listener asked for that.
    show_list_buttons();

    refresh();
}

std::vector<std::filesystem::path> MainWindow::ask_for_tracks()
{
    QStringList patterns;
    for (const std::string& extension : presenter_.playback_extensions())
        patterns.append(QString::fromStdString("*" + extension));

    const QString filter{QString{"Audio (%1);;All files (*)"}.arg(patterns.join(' '))};
    const QStringList chosen{
        QFileDialog::getOpenFileNames(this, "Choose tracks", QString{}, filter)};

    std::vector<std::filesystem::path> tracks;
    tracks.reserve(static_cast<std::size_t>(chosen.size()));

    for (const QString& name : chosen)
        tracks.push_back(QtPathAdapter::from_qt(name));

    return tracks;
}

void MainWindow::choose_tracks()
{
    // Whether the files can be read is said by playing them.
    open(ask_for_tracks());
}

void MainWindow::add_tracks()
{
    std::vector<std::filesystem::path> tracks{ask_for_tracks()};

    if (tracks.empty())
        return;

    for (const std::filesystem::path& location : tracks)
        request_metadata(location);

    presenter_.add(std::move(tracks));
    playlist_.save();
    refresh();
}

void MainWindow::import_m3u_playlist()
{
    const QString selected = QFileDialog::getOpenFileName(this, "Import M3U8 playlist", {},
        "M3U8 playlists (*.m3u8 *.m3u);;All files (*)");
    if (selected.isEmpty())
        return;

    QFile input{selected};
    if (!input.open(QIODevice::ReadOnly)) {
        show_status("Could not open the M3U8 playlist");
        return;
    }

    const QByteArray encoded = input.read(16 * 1024 * 1024 + 1);
    const auto previous_tracks = library_.tracks();
    const auto previous_playlists = library_.playlists();
    const auto previous_workspaces = library_.workspaces();
    library::PlaylistId imported;
    const QFileInfo details{selected};
    const app::M3uError error =
        app::M3uPlaylist::import_playlist(library_, QtPathAdapter::from_qt(details.absolutePath()),
            details.completeBaseName().toStdString(), encoded, imported);

    if (error != app::M3uError::none) {
        show_status("Could not import the M3U8 playlist");
        return;
    }

    if (playlist_storage_.save(library_) != LibraryJsonError::none) {
        static_cast<void>(library_.replace_all(previous_tracks, previous_playlists,
            previous_workspaces));
        show_status("Could not save the imported playlist");
        return;
    }

    navigation_->show_repository(library_);
    open_playlist(imported);
    show_status("M3U8 playlist imported");
}

void MainWindow::export_m3u_playlist()
{
    std::optional<library::PlaylistId> selected;

    for (const auto& [id, document] : playlist_documents_) {
        if (documents_->currentWidget() == document)
            selected = id;
    }

    if (!selected)
        selected = active_playlist_.id();

    if (!selected) {
        show_status("Open a saved playlist to export");
        return;
    }

    const QString file =
        QFileDialog::getSaveFileName(this, "Export M3U8 playlist", {}, "M3U8 playlists (*.m3u8)");
    if (file.isEmpty())
        return;

    QByteArray encoded;
    const std::filesystem::path path = QtPathAdapter::from_qt(file);
    const app::M3uError error =
        app::M3uPlaylist::export_playlist(library_, *selected, path.parent_path(), encoded);

    if (error != app::M3uError::none) {
        show_status("Could not export the M3U8 playlist");
        return;
    }

    QSaveFile output{file};
    if (!output.open(QIODevice::WriteOnly) || output.write(encoded) != encoded.size() ||
        !output.commit()) {
        show_status("Could not write the M3U8 playlist");
        return;
    }

    show_status("M3U8 playlist exported");
}

void MainWindow::remove_chosen()
{
    for (const engine::QueueEntryId id : now_playing_->chosen_tracks()) {
        const auto index = presenter_.playlist().index_of(id);
        if (index)
            static_cast<void>(presenter_.remove(*index));
    }

    playlist_.save();
    refresh();
}

void MainWindow::clear_queue()
{
    presenter_.clear();
    playlist_.save();

    // Emptying the queue puts the order back as it was given, shuffle button and all.
    show_list_buttons();

    refresh();
}

void MainWindow::play_chosen(engine::QueueEntryId id)
{
    const auto index = presenter_.playlist().index_of(id);
    if (index)
        static_cast<void>(presenter_.play_track(*index));

    refresh();
}

void MainWindow::open_playlist(const library::PlaylistId& id)
{
    if (const auto found = playlist_documents_.find(id); found != playlist_documents_.end()) {
        documents_->setCurrentWidget(found->second);
        save_playlist_documents();
        return;
    }

    SavedPlaylistDocument* document = new SavedPlaylistDocument{this};
    if (!document->show_playlist(library_, id)) {
        delete document;
        return;
    }

    playlist_documents_.emplace(id, document);
    document->set_active(active_playlist_.id() == id);
    document->on_active_requested([this, id] { activate_playlist(id); });
    document->on_play_requested([this, id] { play_saved_playlist(id); });
    document->on_apply_order_requested([this, document] {
        play_displayed_paths(document->displayed_paths());
    });
    document->on_transfer_active_requested([this, id, document] {
        transfer_saved_playlist(id, *document, false);
    });
    document->on_transfer_destination_requested([this, id, document] {
        transfer_saved_playlist(id, *document, true);
    });
    document->on_transfer_move_requested([this, id, document] {
        choose_move_destination(id, *document);
    });

    const QString name = QString::fromStdString(library_.playlist(id)->name);
    documents_->addTab(document, name);
    documents_->setCurrentWidget(document);
    save_playlist_documents();
}

void MainWindow::open_folder()
{
    const QString selected = QFileDialog::getExistingDirectory(this, "Open folder workspace");
    if (selected.isEmpty())
        return;

    const auto previous_tracks = library_.tracks();
    const auto previous_playlists = library_.playlists();
    const auto previous_workspaces = library_.workspaces();
    const library::WorkspaceId workspace =
        library_.create_workspace(QtPathAdapter::from_qt(selected));

    if (!workspace.valid())
        return;

    if (playlist_storage_.save(library_) != LibraryJsonError::none) {
        static_cast<void>(library_.replace_all(previous_tracks, previous_playlists,
            previous_workspaces));
        show_status("Could not save the folder workspace");
        return;
    }

    static_cast<void>(workspace_watcher_->watch(*library_.workspace(workspace)));
    show_workspace_navigation();
    open_workspace(workspace);
}

void MainWindow::show_workspace_navigation()
{
    std::vector<WorkspaceNavigationEntry> entries;

    for (const library::WorkspaceRecord& workspace : library_.workspaces()) {
        const std::filesystem::path name = workspace.root.filename();
        entries.push_back({.id = workspace.id,
            .name = name.empty() ? workspace.root.string() : name.string()});
    }

    navigation_->show_workspaces(entries);
}

void MainWindow::open_workspace(const library::WorkspaceId& id)
{
    const library::WorkspaceRecord* workspace = library_.workspace(id);
    if (workspace == nullptr)
        return;

    if (const auto found = workspace_documents_.find(id); found != workspace_documents_.end()) {
        documents_->setCurrentWidget(found->second);
        save_workspace_documents();
        return;
    }

    WorkspaceDocument* document = new WorkspaceDocument{this};
    workspace_documents_.emplace(id, document);
    document->on_rescan_requested([this, id](bool recursive) { rescan_workspace(id, recursive); });
    document->on_cancel_requested([this, document] {
        workspace_scans_->cancel();
        document->show_cancelled();
    });
    document->on_play_requested([this, document] { play_workspace_selection(*document); });
    document->on_append_requested([this, document] { append_workspace_selection(*document); });
    document->on_apply_order_requested([this, document] {
        play_displayed_paths(document->displayed_paths());
    });
    document->on_transfer_active_requested([this, document] {
        transfer_workspace(*document, false);
    });
    document->on_transfer_destination_requested([this, document] {
        transfer_workspace(*document, true);
    });

    const std::filesystem::path name = workspace->root.filename();
    documents_->addTab(document, name.empty() ? "Workspace" : QtPathAdapter::to_qt(name));
    documents_->setCurrentWidget(document);

    const std::uint64_t generation =
        workspace_scans_->start(*workspace, session_.playback_extensions());
    document->begin(*workspace, generation);
    save_workspace_documents();
}

void MainWindow::play_workspace_selection(WorkspaceDocument& document)
{
    const app::PlaybackQueueResult result =
        app::PlaybackQueue{}.replace_with_paths(session_, document.chosen_paths());

    if (!result.requested) {
        show_status("Select workspace tracks to play");
        return;
    }

    said_ = result.open_result;
    playlist_.save();
    refresh();
}

void MainWindow::append_workspace_selection(WorkspaceDocument& document)
{
    const app::PlaybackQueueResult result =
        app::PlaybackQueue{}.append_paths(session_, document.chosen_paths());

    if (!result.requested) {
        show_status("Select workspace tracks to add");
        return;
    }

    playlist_.save();
    show_status(QString{"%1 track(s) added to Now Playing"}.arg(result.queued));
    refresh();
}

void MainWindow::transfer_workspace(WorkspaceDocument& document, bool choose_destination)
{
    std::vector<app::MediaTransfer> media;

    for (const std::filesystem::path& path : document.chosen_paths())
        media.push_back({.media = library::media_id_for(path), .location = path});

    if (choose_destination)
        choose_transfer_destination(media);
    else
        transfer_to({}, media);
}

void MainWindow::rescan_workspace(const library::WorkspaceId& id, bool recursive)
{
    const library::WorkspaceRecord* current = library_.workspace(id);
    WorkspaceDocument* document =
        workspace_documents_.contains(id) ? workspace_documents_.at(id) : nullptr;
    if (current == nullptr || document == nullptr)
        return;

    library::WorkspaceRecord updated = *current;
    updated.recursive = recursive;

    if (recursive != current->recursive) {
        const auto previous_tracks = library_.tracks();
        const auto previous_playlists = library_.playlists();
        const auto previous_workspaces = library_.workspaces();

        if (!library_.put_workspace(updated)) {
            show_status("Could not update the folder workspace");
            return;
        }

        if (playlist_storage_.save(library_) != LibraryJsonError::none) {
            static_cast<void>(library_.replace_all(previous_tracks, previous_playlists,
                previous_workspaces));
            show_status("Could not save the workspace settings");
            return;
        }
    }

    static_cast<void>(workspace_watcher_->watch(updated));

    const std::uint64_t generation =
        workspace_scans_->start(updated, session_.playback_extensions());
    document->begin(updated, generation);
}

void MainWindow::restore_workspace_documents()
{
    const library::WorkspaceId current_id{
        settings_.value("documents/current_workspace").toString().toStdString()};
    const QStringList open = settings_.value("documents/open_workspaces").toStringList();

    for (const QString& id : open)
        open_workspace(library::WorkspaceId{id.toStdString()});

    const auto current = workspace_documents_.find(current_id);
    if (current != workspace_documents_.end())
        documents_->setCurrentWidget(current->second);
}

void MainWindow::save_workspace_documents()
{
    QStringList open;
    QString current;

    for (const auto& [id, document] : workspace_documents_) {
        open.push_back(QString::fromStdString(id.value));
        if (documents_->currentWidget() == document)
            current = QString::fromStdString(id.value);
    }

    settings_.setValue("documents/open_workspaces", open);
    settings_.setValue("documents/current_workspace", current);
    settings_.sync();
}

void MainWindow::activate_playlist(const library::PlaylistId& id)
{
    if (library_.playlist(id) == nullptr)
        return;

    static_cast<void>(active_playlist_.set(id));
    settings_.setValue("playlists/active", QString::fromStdString(id.value));
    settings_.sync();

    for (const auto& [playlist, document] : playlist_documents_)
        document->set_active(playlist == id);
}

void MainWindow::transfer_now_playing(bool choose_destination)
{
    std::vector<app::MediaTransfer> media;

    for (const engine::QueueEntryId id : now_playing_->chosen_tracks()) {
        const auto row = session_.playlist().index_of(id);
        if (!row)
            continue;

        const std::filesystem::path& location = session_.playlist().tracks()[*row].path();
        media.push_back({.media = library::media_id_for(location), .location = location});
    }

    if (choose_destination)
        choose_transfer_destination(media);
    else
        transfer_to({}, media);
}

void MainWindow::transfer_saved_playlist(const library::PlaylistId& id,
    SavedPlaylistDocument& document, bool choose_destination)
{
    const library::PlaylistRecord* source = library_.playlist(id);
    if (source == nullptr)
        return;

    std::vector<app::MediaTransfer> media;

    for (const library::PlaylistEntryId& selected : document.chosen_entries()) {
        const auto entry =
            std::ranges::find(source->entries, selected, &library::PlaylistEntryRecord::id);
        if (entry == source->entries.end())
            continue;

        const library::TrackRecord* track = library_.track(entry->media);
        media.push_back({.media = entry->media,
            .location = track != nullptr && !track->locations.empty() ? track->locations.front()
                                                                      : std::filesystem::path{}});
    }

    if (choose_destination)
        choose_transfer_destination(media);
    else
        transfer_to({}, media);
}

void MainWindow::choose_move_destination(const library::PlaylistId& id,
    SavedPlaylistDocument& document)
{
    const std::vector<library::PlaylistEntryId> entries = document.chosen_entries();
    if (entries.empty()) {
        show_status("Select tracks to move");
        return;
    }

    QMenu destinations{this};

    for (const library::PlaylistRecord& playlist : library_.playlists()) {
        if (playlist.id == id)
            continue;

        const library::PlaylistId destination = playlist.id;
        destinations.addAction(QString::fromStdString(playlist.name),
            [this, id, destination, entries] {
                app::PlaylistTransfer transfer{library_, active_playlist_,
                    [this](const library::LibraryRepository& records) {
                        return playlist_storage_.save(records) == LibraryJsonError::none;
                    }};
                const auto result = transfer.move_to(id, destination, entries);

                show_status(result.succeeded ? QString{"%1 track(s) moved"}.arg(result.added)
                                             : QString{"Could not save the playlist move"});
                if (result.succeeded)
                    refresh_playlist_documents();
            });
    }

    if (destinations.isEmpty())
        destinations.addAction("No other saved playlists")->setEnabled(false);

    destinations.exec(QCursor::pos());
}

void MainWindow::choose_transfer_destination(const std::vector<app::MediaTransfer>& media)
{
    QMenu destinations{this};

    for (const library::PlaylistRecord& playlist : library_.playlists()) {
        const library::PlaylistId id = playlist.id;
        destinations.addAction(QString::fromStdString(playlist.name),
            [this, id, media] { transfer_to(id, media); });
    }

    if (destinations.isEmpty())
        destinations.addAction("No saved playlists")->setEnabled(false);

    destinations.exec(QCursor::pos());
}

void MainWindow::transfer_to(std::optional<library::PlaylistId> destination,
    const std::vector<app::MediaTransfer>& media)
{
    if (media.empty()) {
        show_status("Select tracks to add");
        return;
    }

    if (!destination)
        destination = active_playlist_.id();

    if (!destination) {
        show_status("Choose an active playlist first");
        return;
    }

    app::PlaylistTransfer transfer{library_, active_playlist_,
        [this](const library::LibraryRepository& records) {
            return playlist_storage_.save(records) == LibraryJsonError::none;
        }};
    const app::PlaylistTransferResult result = transfer.add_to(*destination, media);

    if (!result.succeeded) {
        show_status("Could not save the playlist transfer");
        return;
    }

    show_status(QString{"%1 track(s) added"}.arg(result.added));
    refresh_playlist_documents();
}

void MainWindow::play_saved_playlist(const library::PlaylistId& id)
{
    const app::PlaybackQueueResult result =
        app::PlaybackQueue{}.replace_with_saved_playlist(session_, library_, id);

    if (!result.requested) {
        show_status("The saved playlist has no playable tracks");
        refresh();
        return;
    }

    said_ = result.open_result;
    playlist_.save();
    show_status(result.unavailable == 0
            ? QString{"opening saved playlist..."}
            : QString{"%1 unavailable track(s) skipped"}.arg(result.unavailable));
    refresh();
}

void MainWindow::play_displayed_paths(std::vector<std::filesystem::path> tracks)
{
    const app::PlaybackQueueResult result =
        app::PlaybackQueue{}.replace_with_paths(session_, std::move(tracks));

    if (!result.requested) {
        show_status("The displayed document has no playable tracks");
        refresh();
        return;
    }

    said_ = result.open_result;
    playlist_.save();
    show_status("Playing the displayed order");
    refresh();
}

void MainWindow::request_metadata(const std::filesystem::path& location)
{
    if (!media_revisions_)
        return;

    static_cast<void>(media_revisions_->enqueue(library::media_id_for(location), location));
}

void MainWindow::publish_media_revision(app::BackgroundMetadataResult result)
{
    if (!result.metadata) {
        show_status("Could not read metadata for " +
            QtPathAdapter::to_qt(result.location.filename()));
        return;
    }

    const auto previous_tracks = library_.tracks();
    const auto previous_playlists = library_.playlists();
    const auto previous_workspaces = library_.workspaces();
    bool created = false;

    if (library_.track(result.media) == nullptr) {
        library::TrackRecord record;
        record.id = result.media;
        record.locations = {result.location};
        created = library_.put_track(std::move(record));

        if (!created) {
            show_status("Could not add the discovered track to the library");
            return;
        }
    }

    const bool merged = metadata_merge_.merge(library_, result.media, *result.metadata);
    if (!created && !merged)
        return;

    if (playlist_storage_.save(library_) != LibraryJsonError::none) {
        static_cast<void>(library_.replace_all(previous_tracks, previous_playlists,
            previous_workspaces));
        show_status("Could not save the learned metadata");
        return;
    }

    refresh_playlist_documents();

    for (const auto& [id, document] : workspace_documents_)
        static_cast<void>(document->show_media_revision(library_, result.media));
}

void MainWindow::finish_custom_tag(app::CustomTagResult result, const QString& success)
{
    if (!result.succeeded()) {
        switch (result.error) {
        case app::CustomTagError::missing_media:
            show_status("The track is no longer in the library");
            break;
        case app::CustomTagError::missing_tag:
            show_status("The custom tag is no longer available");
            break;
        case app::CustomTagError::invalid_value:
            show_status("Enter a valid, non-empty custom tag");
            break;
        case app::CustomTagError::duplicate_value:
            show_status("That custom tag already exists");
            break;
        case app::CustomTagError::limit_exceeded:
            show_status("The track already has the maximum number of custom tags");
            break;
        case app::CustomTagError::persistence_failed:
            show_status("Could not save the custom tag change");
            break;
        case app::CustomTagError::none:
            break;
        }

        return;
    }

    if (!result.changed) {
        show_status("No custom tag changes to save");
        return;
    }

    refresh_playlist_documents();

    for (const auto& [id, document] : workspace_documents_)
        document->show_library(library_);

    refresh();
    show_status(success);
}

std::vector<const library::TrackRecord*> MainWindow::selected_records()
{
    std::vector<const library::TrackRecord*> selected;

    for (const auto& [id, document] : playlist_documents_) {
        if (documents_->currentWidget() != document)
            continue;

        for (const library::PlaylistEntryId& entry : document->chosen_entries()) {
            const auto& entries = library_.playlist(id)->entries;
            const auto found = std::ranges::find(entries, entry, &library::PlaylistEntryRecord::id);
            if (found != entries.end())
                selected.push_back(library_.track(found->media));
        }
    }

    for (const auto& [id, document] : workspace_documents_) {
        if (documents_->currentWidget() != document)
            continue;

        for (const std::filesystem::path& path : document->chosen_paths())
            selected.push_back(library_.track(library::media_id_for(path)));
    }

    if (documents_->currentWidget() == now_playing_) {
        for (const engine::QueueEntryId entry : now_playing_->chosen_tracks()) {
            const auto row = session_.playlist().index_of(entry);
            if (row) {
                selected.push_back(
                    library_.track(library::media_id_for(session_.playlist().tracks()[*row].path()))
                );
            }
        }
    }

    const auto without_missing = std::ranges::remove_if(selected,
        [](const library::TrackRecord* track) { return track == nullptr; });
    selected.erase(without_missing.begin(), without_missing.end());

    if (selected.empty()) {
        if (const engine::Track* current = session_.playlist().current()) {
            if (const library::TrackRecord* record =
                    library_.track(library::media_id_for(current->path())))
                selected.push_back(record);
        }
    }

    return selected;
}

void MainWindow::finish_standard_metadata(library::MediaId, library::StandardFieldOverrides edit)
{
    std::vector<library::MediaId> selected;
    std::map<library::MediaId, app::ExpectedMediaState> expected;

    for (const library::TrackRecord* record : selected_records()) {
        selected.push_back(record->id);
        expected.emplace(record->id,
            app::ExpectedMediaState{.metadata_revision = record->metadata_revision,
                .content_revision = record->content_revision,
                .fingerprint = record->fingerprint});
    }

    pending_metadata_edit_ = PendingMetadataEdit{.media = selected, .edit = edit};

    const auto outcome = optimistic_edits_.submit(selected, edit, expected);
    if (outcome.state == app::MetadataEditState::failed ||
        outcome.state == app::MetadataEditState::conflicted)
        inspector_->standard_metadata()->show_status(outcome.state ==
                    app::MetadataEditState::conflicted
                ? QString{"The track changed since editing started; review before applying"}
                : QString{"Could not save the metadata changes"});
}

void MainWindow::resolve_metadata_conflict(MetadataConflictAction action)
{
    if (action == MetadataConflictAction::review)
        return;

    if (!pending_metadata_edit_)
        return;

    if (action == MetadataConflictAction::keep) {
        pending_metadata_edit_.reset();
        inspector_->conflicts()->hide_conflict();
        show_status("Kept current application metadata");
        return;
    }

    app::MetadataEditOutcome outcome;

    if (action == MetadataConflictAction::reload) {
        outcome = optimistic_edits_.reset(pending_metadata_edit_->media);
    } else {
        outcome =
            optimistic_edits_.submit(pending_metadata_edit_->media, pending_metadata_edit_->edit);
    }

    if (outcome.state == app::MetadataEditState::succeeded)
        pending_metadata_edit_.reset();
}

void MainWindow::publish_metadata_outcome(const app::MetadataEditOutcome& outcome)
{
    refresh_playlist_documents();

    for (const auto& [id, document] : workspace_documents_) {
        for (const library::TrackRecord* record : selected_records())
            static_cast<void>(document->show_media_revision(library_, record->id));
    }

    refresh();

    switch (outcome.state) {
    case app::MetadataEditState::queued:
        inspector_->conflicts()->hide_conflict();
        show_status(QString{"Saving metadata for %1 track(s)"}.arg(outcome.changed));
        break;
    case app::MetadataEditState::succeeded:
        inspector_->conflicts()->hide_conflict();
        if (outcome.changed == 0) {
            show_status(outcome.missing == 0
                    ? QString{"No metadata changes to save"}
                    : QString{"No selected tracks are available in the library"});
        } else {
            show_status(outcome.missing == 0
                    ? QString{"Metadata updated for %1 track(s)"}.arg(outcome.changed)
                    : QString{"Metadata updated for %1 track(s); %2 unavailable"}
                          .arg(outcome.changed)
                          .arg(outcome.missing));
        }
        break;
    case app::MetadataEditState::failed:
        inspector_->conflicts()->hide_conflict();
        show_status(outcome.error == app::StandardMetadataError::empty_edit
                ? QString{"There are no metadata changes to apply"}
                : QString{"Could not save the metadata changes"});
        break;
    case app::MetadataEditState::conflicted:
        inspector_->conflicts()->show_conflict(
            "Embedded metadata changed after editing started. Choose how to resolve it."
        );
        show_status("The track changed since editing started; review before applying");
        break;
    }
}

void MainWindow::refresh_playlist_documents()
{
    for (auto it = playlist_documents_.begin(); it != playlist_documents_.end();) {
        const library::PlaylistRecord* playlist = library_.playlist(it->first);
        SavedPlaylistDocument* document = it->second;

        if (playlist == nullptr) {
            documents_->removeTab(documents_->indexOf(document));
            document->deleteLater();

            if (active_playlist_.clear_if(it->first))
                settings_.remove("playlists/active");

            it = playlist_documents_.erase(it);
            continue;
        }

        static_cast<void>(document->show_playlist(library_, it->first));
        documents_->setTabText(documents_->indexOf(document),
            QString::fromStdString(playlist->name));
        ++it;
    }

    save_playlist_documents();
}

void MainWindow::restore_playlist_documents()
{
    const library::PlaylistId current_id{
        settings_.value("documents/current_playlist").toString().toStdString()};
    const QStringList open = settings_.value("documents/open_playlists").toStringList();
    for (const QString& id : open)
        open_playlist(library::PlaylistId{id.toStdString()});

    const auto current = playlist_documents_.find(current_id);
    if (current != playlist_documents_.end())
        documents_->setCurrentWidget(current->second);
    else
        documents_->setCurrentIndex(0);
}

void MainWindow::save_playlist_documents()
{
    QStringList open;
    QString current;

    for (const auto& [id, document] : playlist_documents_) {
        open.push_back(QString::fromStdString(id.value));
        if (documents_->currentWidget() == document)
            current = QString::fromStdString(id.value);
    }

    settings_.setValue("documents/open_playlists", open);
    settings_.setValue("documents/current_playlist", current);
    settings_.sync();
}

void MainWindow::play_previous()
{
    static_cast<void>(presenter_.previous_track());
    refresh();
}

void MainWindow::play_next()
{
    static_cast<void>(presenter_.next_track());
    refresh();
}

void MainWindow::cycle_repeat()
{
    presenter_.cycle_repeat();

    show_list_buttons();
}

void MainWindow::set_shuffled(bool shuffled)
{
    presenter_.shuffle(shuffled);
    show_list_buttons();
}

void MainWindow::connect_action(QPushButton& button, PlayerAction id, bool icon_only)
{
    QAction* command{actions_.action(id)};

    addAction(command);
    button.setToolTip(command->toolTip());
    button.setAccessibleName(command->text());
    button.setFocusPolicy(Qt::StrongFocus);

    if (icon_only) {
        button.setIcon(command->icon());
        button.setFixedWidth(tokens.player.icon_button_width);
    } else {
        button.setText(command->text());
        button.setCheckable(command->isCheckable());
        button.setChecked(command->isChecked());
    }

    connect(&button, &QPushButton::clicked, command, &QAction::trigger);
}

void MainWindow::show_play_button(bool pausing)
{
    if (showing_pause_ == pausing)
        return;

    showing_pause_ = pausing;
    actions_.set_playing(pausing);

    QAction* command{actions_.action(PlayerAction::play)};

    play_button_->setIcon(command->icon());
    play_button_->setToolTip(command->toolTip());
    play_button_->setAccessibleName(command->text());
}

void MainWindow::show_list_buttons()
{
    const PlayerPresentation presentation{presenter_.presentation()};

    actions_.set_repeat(presentation.repeat);
    actions_.set_shuffled(presentation.shuffled);

    repeat_button_->setText(actions_.action(PlayerAction::repeat)->text());
    shuffle_button_->setChecked(actions_.action(PlayerAction::shuffle)->isChecked());
}

void MainWindow::update_after_pump()
{
    const engine::PlaybackSnapshot playback{presenter_.presentation().playback};
    const engine::OpenResult result{playback.open_result};

    // Said once: a window that repeated itself every turn would overwrite whatever else it has
    // to say.
    if (result != said_ && result != engine::OpenResult::loading) {
        said_ = result;

        const bool opened{result == engine::OpenResult::opened};

        show_status(opened ? QString{} : as_status(result));

        if (opened)
            position_bar_->set_fraction(0.0);
    }

    // The title follows what is playing, so a list moving on to the next track renames the
    // window without anyone asking it to.
    if (playback.track != named_ || presenter_.playlist().position() != named_place_) {
        named_ = playback.track;
        named_place_ = presenter_.playlist().position();

        const QString name{
            named_.empty() ? QString{"no track"} : QtPathAdapter::to_qt(named_.filename())};

        setWindowTitle(named_.empty() ? QString{"Wiola Player"} : name);
        track_label_->setText(name);
        position_bar_->set_fraction(0.0);
        said_fault_ = false;

        // Where the queue stands has moved, which is worth keeping even if nothing was added.
        playlist_.save();
    }

    // A device that goes away is not a listener pressing stop, and is the one thing about
    // playback worth interrupting them for.
    const bool faulted{playback.state == engine::Playback::State::faulted};

    if (faulted && !said_fault_) {
        said_fault_ = true;

        show_status(QString{"lost the playback device"});
    }
}

void MainWindow::toggle_playback()
{
    if (!loaded())
        return;

    // Pausing cannot fail, so a refusal is always an output that would not open.
    const bool pausing{presenter_.presentation().playback.playing};
    const engine::CommandResult result{presenter_.toggle_playback()};

    show_status(result == engine::CommandResult::accepted || pausing
            ? QString{}
            : QString{"no playback device"});
}

void MainWindow::stop_playback()
{
    if (!loaded())
        return;

    presenter_.stop();
}

void MainWindow::show_equalizer()
{
    if (equalizer_panel_ == nullptr)
        equalizer_panel_ = new EqualizerPanel{equalizer_, this};

    equalizer_panel_->show();
    equalizer_panel_->raise();
}

void MainWindow::set_volume(int percent)
{
    volume_.set_position(percent);

    show_volume();
}

void MainWindow::show_volume()
{
    volume_value_->setText(QString::number(volume_.position()) + "%");
}

void MainWindow::set_boosted(bool boosted)
{
    volume_.set_boosted(boosted);

    // The slider is asked to hold what the control now allows, and says what it settled on.
    volume_slider_->setRange(0, volume_.max_position());
    volume_slider_->setValue(volume_.position());

    show_volume();
}

void MainWindow::seek_to(double fraction)
{
    presenter_.seek_to(fraction);
}

void MainWindow::show_status(const QString& message)
{
    status_label_->setText(message);
}

void MainWindow::refresh()
{
    const engine::PlaybackSnapshot playback{presenter_.presentation().playback};

    actions_.set_available(PlayerAction::play, playback.loaded);
    actions_.set_available(PlayerAction::stop, playback.loaded);
    actions_.set_available(PlayerAction::previous, playback.loaded);
    actions_.set_available(PlayerAction::next, playback.loaded);

    play_button_->setEnabled(actions_.action(PlayerAction::play)->isEnabled());
    stop_button_->setEnabled(actions_.action(PlayerAction::stop)->isEnabled());
    previous_button_->setEnabled(actions_.action(PlayerAction::previous)->isEnabled());
    next_button_->setEnabled(actions_.action(PlayerAction::next)->isEnabled());
    position_bar_->setEnabled(playback.loaded && playback.seekable &&
        playback.total_time.has_value());

    now_playing_->show_library(library_);
    now_playing_->show_playlist(presenter_.playlist());

    const engine::Track* current = presenter_.playlist().current();
    const library::TrackRecord* record =
        current != nullptr ? library_.track(library::media_id_for(current->path())) : nullptr;
    inspector_->show_track(current);
    inspector_->show_custom_tags(record);
    inspector_->show_standard_metadata(selected_records());

    if (!playback.loaded) {
        show_play_button(false);
        track_label_->setText("no track");
        elapsed_label_->clear();
        total_label_->clear();

        return;
    }

    const std::optional<units::Time> total{playback.total_time};

    // While the edge is being dragged it is a place being chosen, and that is the time worth
    // showing: a listener aiming for somewhere needs to see where they are aiming.
    const units::Time shown{position_bar_->dragging() && total ? *total * position_bar_->fraction()
                                                               : playback.time_played};

    show_play_button(playback.playing);

    elapsed_label_->setText(as_clock(shown));
    total_label_->setText(total ? as_clock(*total) : QString{"--:--"});

    if (!total || *total == units::Time{})
        return;

    // A drag in progress is left alone by the bar itself.
    position_bar_->set_fraction(shown.get<units::Sec>() / total->get<units::Sec>());
}

} // namespace wiola::gui
