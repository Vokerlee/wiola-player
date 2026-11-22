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

#include <gui/appearance/presentation_tokens.hpp>
#include <gui/appearance/theme.hpp>
#include <gui/appearance/theme_editor.hpp>
#include <gui/controls/clock.hpp>
#include <gui/layout/classic_layout.hpp>
#include <gui/layout/editor/button_layout_editor.hpp>
#include <gui/layout/focused_layout.hpp>
#include <gui/layout/layout_builder.hpp>
#include <platform/qt/player_settings.hpp>
#include <platform/qt/qt_path_adapter.hpp>

#include <library/location.hpp>
#include <library/metadata.hpp>

#include <QActionGroup>
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
#include <array>
#include <cmath>
#include <optional>
#include <utility>

namespace wiola::gui {

namespace {

constexpr PresentationTokens tokens;

LayoutDocument layout_for(const QString& choice)
{
    if (choice == "library-focus")
        return library_focus_layout_document();

    if (choice == "queue-focus")
        return queue_focus_layout_document();

    return classic_layout_document();
}

constexpr std::array theme_color_roles{
    ThemeColorRole::window,
    ThemeColorRole::surface,
    ThemeColorRole::text,
    ThemeColorRole::muted_text,
    ThemeColorRole::disabled_text,
    ThemeColorRole::accent,
    ThemeColorRole::accent_text,
    ThemeColorRole::focus,
    ThemeColorRole::error,
};

ThemeColors stored_custom_theme(const QSettings& settings)
{
    ThemeColors colors = theme_colors(Theme::light);

    for (const ThemeColorRole role : theme_color_roles) {
        const QString key = "theme/custom/" + QString::fromLatin1(theme_color_key(role));
        const QColor stored{settings.value(key).toString()};
        if (stored.isValid())
            theme_color(colors, role) = stored;
    }

    return colors;
}

void store_custom_theme(QSettings& settings, const ThemeColors& colors)
{
    for (const ThemeColorRole role : theme_color_roles) {
        settings.setValue("theme/custom/" + QString::fromLatin1(theme_color_key(role)),
            theme_color(colors, role).name());
    }
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

MainWindow::MainWindow(app::PlaybackService& playback_service, app::LibrarySession& library_session,
    app::PlaybackPreferences& preferences)
    // A file of its own on both platforms, rather than the registry on one of them.
    : playback_{playback_service}
    , actions_{*this}
    , settings_{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"}
    , playlist_{preferences.queue()}
    , volume_{preferences.volume()}
    , equalizer_{preferences.equalizer()}
    , library_session_{library_session}
    , library_{library_session.repository()}
    , persist_library_{library_session.persistence()}
    , playlist_commands_{library_session.playlists()}
    , tag_commands_{library_session.tags()}
    , optimistic_edits_{library_session.metadata_edits()}
    , active_playlist_{library_session.active_playlist()}
    , workspace_watcher_{&library_session.watcher()}
{
    const QString selected_theme = settings_.value("theme/name", "light").toString();
    const ThemeColors custom_theme = stored_custom_theme(settings_);

    if (selected_theme == "custom" && theme_contrast_error(custom_theme).isEmpty())
        apply_theme(custom_theme);
    else
        apply_theme(selected_theme == "dark" ? Theme::dark : Theme::light);

    setWindowTitle("Wiola Player");
    setMinimumSize(tokens.player.window_min_width, tokens.player.window_min_height);

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
    track_label_ = new QLabel{this};
    elapsed_label_ = new QLabel{this};
    total_label_ = new QLabel{this};
    status_label_ = new QLabel{this};
    volume_slider_ = new QSlider{Qt::Horizontal, this};

    if (!library_session_.restore_error().isEmpty())
        show_status(library_session_.restore_error());

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

    DocumentCommands document_commands;
    document_commands.open_folder = [this] {
        open_folder();
    };
    document_commands.activate_playlist = [this](const auto& id) {
        activate_playlist(id);
    };
    document_commands.play_playlist = [this](const auto& id) {
        play_saved_playlist(id);
    };
    document_commands.transfer_playlist = [this](const auto& id, auto& document, bool choose) {
        transfer_saved_playlist(id, document, choose);
    };
    document_commands.move_playlist = [this](const auto& id, auto& document) {
        choose_move_destination(id, document);
    };
    document_commands.rescan = [this](const auto& id, bool recursive) {
        rescan_workspace(id, recursive);
    };
    document_commands.play_workspace = [this](auto& document) {
        play_workspace_selection(document);
    };
    document_commands.append_workspace = [this](auto& document) {
        append_workspace_selection(document);
    };
    document_commands.transfer_workspace = [this](auto& document, bool choose) {
        transfer_workspace(document, choose);
    };
    document_commands.apply_order = [this](auto paths) {
        play_displayed_paths(std::move(paths));
    };
    document_host_ =
        new DocumentHost{library_session_, playback_, std::move(document_commands), this};
    documents_ = &document_host_->tabs();
    now_playing_ = &document_host_->queue();

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
                                                  library::StandardMetadataPatch edit) {
        finish_standard_metadata(media, std::move(edit));
    });
    inspector_->conflicts()->on_action([this](MetadataConflictAction action) {
        resolve_metadata_conflict(action);
    });

    connect(&library_session_, &app::LibrarySession::metadata_outcome, this,
        &MainWindow::publish_metadata_outcome);
    connect(&library_session_, &app::LibrarySession::status_changed, this,
        &MainWindow::show_status);
    connect(&library_session_, &app::LibrarySession::workspace_changed, this,
        [this](const library::WorkspaceId& id) {
            if (const auto* workspace = library_.workspace(id))
                rescan_workspace(id, workspace->recursive);
        });

    library_pane_ = new QWidget{this};
    QVBoxLayout* library_layout = new QVBoxLayout{library_pane_};
    library_layout->setContentsMargins(0, 0, 0, 0);
    library_layout->addWidget(navigation_);
    library_layout->addWidget(playlist_manager_);

    document_area_ = new QSplitter{this};
    document_area_->setObjectName("documentArea");
    document_area_->addWidget(library_pane_);
    document_area_->addWidget(document_host_);
    document_area_->addWidget(inspector_);
    document_area_->setStretchFactor(0, 0);
    document_area_->setStretchFactor(1, 1);
    document_area_->setStretchFactor(2, 0);

    navigation_->on_playlist_chosen([this](const auto& id) {
        static_cast<void>(document_host_->open_playlist(id));
    });
    navigation_->on_workspace_chosen([this](const auto& id) {
        static_cast<void>(document_host_->open_workspace(id));
    });

    const QString selected_layout = settings_.value("layout/preset", "classic").toString();
    static_cast<void>(switch_layout(layout_for(selected_layout)));

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

    QMenu* view = menuBar()->addMenu("&View");
    QAction* classic = view->addAction("Classic");
    QAction* library = view->addAction("Library Focus");
    QAction* queue = view->addAction("Queue Focus");
    auto* presets = new QActionGroup{this};
    presets->setExclusive(true);

    for (QAction* preset : {classic, library, queue}) {
        preset->setCheckable(true);
        presets->addAction(preset);
    }

    classic->setChecked(selected_layout != "library-focus" && selected_layout != "queue-focus");
    library->setChecked(selected_layout == "library-focus");
    queue->setChecked(selected_layout == "queue-focus");

    const auto choose_layout = [this](const QString& choice) {
        if (!switch_layout(layout_for(choice)))
            return;

        settings_.setValue("layout/preset", choice);
        settings_.sync();
    };

    connect(classic, &QAction::triggered, this, [choose_layout] { choose_layout("classic"); });
    connect(library, &QAction::triggered, this,
        [choose_layout] { choose_layout("library-focus"); });
    connect(queue, &QAction::triggered, this, [choose_layout] { choose_layout("queue-focus"); });

    view->addSeparator();
    QAction* light = view->addAction("Light theme");
    QAction* dark = view->addAction("Dark theme");
    auto* themes = new QActionGroup{this};
    themes->setExclusive(true);

    for (QAction* theme : {light, dark}) {
        theme->setCheckable(true);
        themes->addAction(theme);
    }

    light->setChecked(selected_theme != "dark" && selected_theme != "custom");
    dark->setChecked(selected_theme == "dark");

    const auto refresh_theme_icons = [this] {
        actions_.reload_icons(*this);
        open_button_->setIcon(actions_.action(PlayerAction::open)->icon());
        previous_button_->setIcon(actions_.action(PlayerAction::previous)->icon());
        play_button_->setIcon(actions_.action(PlayerAction::play)->icon());
        next_button_->setIcon(actions_.action(PlayerAction::next)->icon());
        stop_button_->setIcon(actions_.action(PlayerAction::stop)->icon());
    };

    const auto choose_theme = [this, refresh_theme_icons](const QString& choice) {
        apply_theme(choice == "dark" ? Theme::dark : Theme::light);
        refresh_theme_icons();
        settings_.setValue("theme/name", choice);
        settings_.sync();
    };

    connect(light, &QAction::triggered, this, [choose_theme] { choose_theme("light"); });
    connect(dark, &QAction::triggered, this, [choose_theme] { choose_theme("dark"); });

    QMenu* tools = menuBar()->addMenu("&Tools");
    tools->addAction(actions_.action(PlayerAction::equalizer));
    QAction* edit_layout = tools->addAction("Edit button layout...");
    connect(edit_layout, &QAction::triggered, this, [this] {
        auto* editor = new ButtonLayoutEditor{
            layout_for(settings_.value("layout/preset", "classic").toString()), this};
        editor->setAttribute(Qt::WA_DeleteOnClose);
        editor->on_apply([this](const LayoutDocument& document) {
            return switch_layout(document);
        });
        editor->show();
    });

    QAction* edit_theme = tools->addAction("Edit theme...");
    connect(edit_theme, &QAction::triggered, this,
        [this, refresh_theme_icons, light, dark, initial = custom_theme] {
            auto* editor = new ThemeEditor{initial, this};
            editor->setAttribute(Qt::WA_DeleteOnClose);
            editor->on_apply([this, refresh_theme_icons, light, dark](const ThemeColors& colors) {
                apply_theme(colors);
                refresh_theme_icons();
                store_custom_theme(settings_, colors);
                settings_.setValue("theme/name", "custom");
                settings_.sync();
                light->setChecked(false);
                dark->setChecked(false);
            });
            editor->show();
        });

    connect(now_playing_, &NowPlayingDocument::track_chosen, this, &MainWindow::play_chosen);
    connect(now_playing_, &NowPlayingDocument::removal_asked, this, &MainWindow::remove_chosen);
    connect(now_playing_, &NowPlayingDocument::transfer_active_asked, this,
        [this] { transfer_now_playing(false); });
    connect(now_playing_, &NowPlayingDocument::transfer_destination_asked, this,
        [this] { transfer_now_playing(true); });
    connect(now_playing_, &NowPlayingDocument::order_application_asked, this,
        [this] { play_displayed_paths(now_playing_->displayed_paths(playback_.playlist())); });

    connect(volume_slider_, &QSlider::valueChanged, this, &MainWindow::set_volume);
    connect(boost_button_, &QPushButton::toggled, this, &MainWindow::set_boosted);

    connect(position_bar_, &SeekBar::seek_requested, this, &MainWindow::seek_to);

    // The clock follows the drag rather than waiting for the next poll.
    connect(position_bar_, &SeekBar::dragged, this, &MainWindow::refresh);

    if (const std::size_t gone{preferences.missing_tracks()}; gone != 0)
        show_status(QString{"%1 queued track(s) are no longer where they were"}.arg(gone));

    // What was put back settles both of these, without either button having been pressed.
    show_list_buttons();
    library_changes_ = library_.observe([this](const library::LibraryChangeBatch& batch) {
        apply_library_changes(batch);
    });

    refresh();
}

MainWindow::~MainWindow() = default;

bool MainWindow::switch_layout(const LayoutDocument& document)
{
    if (document.node_count == 0) {
        show_status("Could not construct the layout");
        return false;
    }

    LayoutBuilder builder;
    const std::array<std::pair<const char*, QWidget*>, 26> widgets{
        std::pair{"trackLabel",           track_label_     },
        std::pair{"positionBar",          position_bar_    },
        std::pair{"elapsedTime",          elapsed_label_   },
        std::pair{"totalDuration",        total_label_     },
        std::pair{"previousTrack",        previous_button_ },
        std::pair{"playPause",            play_button_     },
        std::pair{"nextTrack",            next_button_     },
        std::pair{"stopPlayback",         stop_button_     },
        std::pair{"volumeSlider",         volume_slider_   },
        std::pair{"volumeValue",          volume_value_    },
        std::pair{"boostButton",          boost_button_    },
        std::pair{"openTracks",           open_button_     },
        std::pair{"repeatQueue",          repeat_button_   },
        std::pair{"shuffleQueue",         shuffle_button_  },
        std::pair{"equalizer",            equalizer_button_},
        std::pair{"documentArea",         document_area_   },
        std::pair{"libraryPane",          library_pane_    },
        std::pair{"libraryNavigation",    navigation_      },
        std::pair{"savedPlaylistManager", playlist_manager_},
        std::pair{"documentTabs",         document_host_   },
        std::pair{"trackInspector",       inspector_       },
        std::pair{"nowPlayingDocument",   now_playing_     },
        std::pair{"addTracks",            add_button_      },
        std::pair{"removeTracks",         remove_button_   },
        std::pair{"clearQueue",           clear_button_    },
        std::pair{"playerStatus",         status_label_    },
    };

    for (const auto& [id, widget] : widgets)
        static_cast<void>(builder.register_widget(QString::fromLatin1(id), widget));

    static_cast<void>(builder.register_widget("nowPlayingSearch",
        now_playing_->findChild<QWidget*>("trackSearchPanel")));
    static_cast<void>(builder.register_widget("nowPlayingFilters",
        now_playing_->findChild<QWidget*>("tagFilterPanel")));

    for (const auto& [id, saved] : document_host_->registry().playlists()) {
        const QString identity = QString::fromStdString(id.value);
        static_cast<void>(builder.register_widget("playlistDocument:" + identity, saved));
        static_cast<void>(builder.register_widget("playlistSearch:" + identity,
            saved->findChild<QWidget*>("trackSearchPanel")));
        static_cast<void>(builder.register_widget("playlistFilters:" + identity,
            saved->findChild<QWidget*>("tagFilterPanel")));
    }

    for (const auto& [id, workspace] : document_host_->registry().workspaces()) {
        const QString identity = QString::fromStdString(id.value);
        static_cast<void>(builder.register_widget("workspaceDocument:" + identity, workspace));
        static_cast<void>(builder.register_widget("workspaceSearch:" + identity,
            workspace->findChild<QWidget*>("trackSearchPanel")));
        static_cast<void>(builder.register_widget("workspaceFilters:" + identity,
            workspace->findChild<QWidget*>("tagFilterPanel")));
    }

    QWidget* root =
        builder.build(document.root, {.actions = &actions_, .library = &library_}, this);

    if (root == nullptr) {
        show_status("Could not construct the layout");
        return false;
    }

    const auto belongs_to_root = [root](QWidget* widget) {
        for (QWidget* current = widget; current != nullptr; current = current->parentWidget()) {
            if (current == root)
                return true;
        }

        return false;
    };

    for (const auto& [id, widget] : widgets) {
        if (!belongs_to_root(widget)) {
            widget->setParent(this);
            widget->hide();
        }
    }

    if (QLayout* layout = root->layout())
        layout->setSpacing(tokens.player.section_spacing);

    setCentralWidget(root);
    return true;
}

void MainWindow::open(std::vector<std::filesystem::path> tracks)
{
    if (tracks.empty())
        return;

    const QString first{platform::qt::QtPathAdapter::to_qt(tracks.front().filename())};

    for (const std::filesystem::path& location : tracks)
        request_metadata(location);

    static_cast<void>(playback_.open(std::move(tracks)));
    said_ = app::present_playback(playback_).playback.open_result;

    playlist_.save();
    show_status(QString{"opening "} + first + "...");

    // A new list is an unshuffled one, whether or not the listener asked for that.
    show_list_buttons();

    refresh();
}

std::vector<std::filesystem::path> MainWindow::ask_for_tracks()
{
    QStringList patterns;
    for (const std::string& extension : playback_.playback_extensions())
        patterns.append(QString::fromStdString("*" + extension));

    const QString filter{QString{"Audio (%1);;All files (*)"}.arg(patterns.join(' '))};
    const QStringList chosen{
        QFileDialog::getOpenFileNames(this, "Choose tracks", QString{}, filter)};

    std::vector<std::filesystem::path> tracks;
    tracks.reserve(static_cast<std::size_t>(chosen.size()));

    for (const QString& name : chosen)
        tracks.push_back(platform::qt::QtPathAdapter::from_qt(name));

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

    playback_.add(std::move(tracks));
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
    library::RepositoryTransaction transaction{library_, persist_library_};
    library::PlaylistId imported;
    const QFileInfo details{selected};
    const app::M3uError error = app::M3uPlaylist::import_playlist(library_,
        platform::qt::QtPathAdapter::from_qt(details.absolutePath()),
        details.completeBaseName().toStdString(), encoded, imported);

    if (error != app::M3uError::none) {
        show_status("Could not import the M3U8 playlist");
        return;
    }

    if (!transaction.commit()) {
        show_status("Could not save the imported playlist");
        return;
    }

    navigation_->show_repository(library_);
    static_cast<void>(document_host_->open_playlist(imported));
    show_status("M3U8 playlist imported");
}

void MainWindow::export_m3u_playlist()
{
    std::optional<library::PlaylistId> selected;

    if (const auto current = document_host_->registry().active(); current && current->playlist())
        selected = *current->playlist();

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
    const std::filesystem::path path = platform::qt::QtPathAdapter::from_qt(file);
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
        const auto index = playback_.playlist().index_of(id);
        if (index)
            static_cast<void>(playback_.remove(*index));
    }

    playlist_.save();
    refresh();
}

void MainWindow::clear_queue()
{
    playback_.clear();
    playlist_.save();

    // Emptying the queue puts the order back as it was given, shuffle button and all.
    show_list_buttons();

    refresh();
}

void MainWindow::play_chosen(engine::QueueEntryId id)
{
    const auto index = playback_.playlist().index_of(id);
    if (index)
        static_cast<void>(playback_.play_track(*index));

    refresh();
}

void MainWindow::open_folder()
{
    const QString selected = QFileDialog::getExistingDirectory(this, "Open folder workspace");
    if (selected.isEmpty())
        return;

    library::RepositoryTransaction transaction{library_, persist_library_};
    const library::WorkspaceId workspace =
        library_.create_workspace(platform::qt::QtPathAdapter::from_qt(selected));

    if (!workspace.valid())
        return;

    if (!transaction.commit()) {
        show_status("Could not save the folder workspace");
        return;
    }

    static_cast<void>(workspace_watcher_->watch(*library_.workspace(workspace)));
    show_workspace_navigation();
    static_cast<void>(document_host_->open_workspace(workspace));
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

void MainWindow::play_workspace_selection(WorkspaceDocument& document)
{
    const app::PlaybackQueueResult result =
        app::PlaybackQueue{}.replace_with_paths(playback_, document.chosen_paths());

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
        app::PlaybackQueue{}.append_paths(playback_, document.chosen_paths());

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
    WorkspaceDocument* document = document_host_->registry().workspace(id);
    if (current == nullptr || document == nullptr)
        return;

    library::WorkspaceRecord updated = *current;
    updated.recursive = recursive;

    if (recursive != current->recursive) {
        library::RepositoryTransaction transaction{library_, persist_library_};

        if (!library_.put_workspace(updated)) {
            show_status("Could not update the folder workspace");
            return;
        }

        if (!transaction.commit()) {
            show_status("Could not save the workspace settings");
            return;
        }
    }

    static_cast<void>(workspace_watcher_->watch(updated));

    document_host_->begin_scan(id);
}

void MainWindow::activate_playlist(const library::PlaylistId& id)
{
    if (library_.playlist(id) == nullptr)
        return;

    static_cast<void>(active_playlist_.set(id));
    settings_.setValue("playlists/active", QString::fromStdString(id.value));
    settings_.sync();

    document_host_->refresh_active_destination();
}

void MainWindow::transfer_now_playing(bool choose_destination)
{
    std::vector<app::MediaTransfer> media;

    for (const engine::QueueEntryId id : now_playing_->chosen_tracks()) {
        const auto row = playback_.playlist().index_of(id);
        if (!row)
            continue;

        const std::filesystem::path& location = playback_.playlist().tracks()[*row].path();
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
                app::PlaylistTransfer transfer{library_, active_playlist_, persist_library_};
                const auto result = transfer.move_to(id, destination, entries);

                show_status(result.succeeded ? QString{"%1 track(s) moved"}.arg(result.added)
                                             : QString{"Could not save the playlist move"});
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

    app::PlaylistTransfer transfer{library_, active_playlist_, persist_library_};
    const app::PlaylistTransferResult result = transfer.add_to(*destination, media);

    if (!result.succeeded) {
        show_status("Could not save the playlist transfer");
        return;
    }

    show_status(QString{"%1 track(s) added"}.arg(result.added));
}

void MainWindow::play_saved_playlist(const library::PlaylistId& id)
{
    const app::PlaybackQueueResult result =
        app::PlaybackQueue{}.replace_with_saved_playlist(playback_, library_, id);

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
        app::PlaybackQueue{}.replace_with_paths(playback_, std::move(tracks));

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
    library_session_.request_metadata(location);
}

void MainWindow::apply_library_changes(const library::LibraryChangeBatch& batch)
{
    for (const auto& id : batch.removed_playlists) {
        if (active_playlist_.clear_if(id))
            settings_.remove("playlists/active");
    }
    if (!batch.playlists.empty() || !batch.removed_playlists.empty())
        navigation_->show_repository(library_);
    if (!batch.workspaces.empty() || !batch.removed_workspaces.empty())
        show_workspace_navigation();
    refresh();
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

    show_status(success);
}

std::vector<const library::TrackRecord*> MainWindow::selected_records()
{
    std::vector<const library::TrackRecord*> selected;
    for (const auto& item : document_host_->registry().selection())
        if (const auto* record = library_.track(item.media))
            selected.push_back(record);
    if (selected.empty()) {
        if (const auto* current = playback_.playlist().current())
            if (const auto* record = library_.track(library::media_id_for(current->path())))
                selected.push_back(record);
    }
    return selected;
}

void MainWindow::finish_standard_metadata(library::MediaId, library::StandardMetadataPatch edit)
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

void MainWindow::play_previous()
{
    static_cast<void>(playback_.previous_track());
    refresh();
}

void MainWindow::play_next()
{
    static_cast<void>(playback_.next_track());
    refresh();
}

void MainWindow::cycle_repeat()
{
    playback_.cycle_repeat();

    show_list_buttons();
}

void MainWindow::set_shuffled(bool shuffled)
{
    playback_.shuffle(shuffled);
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
    const app::PlayerPresentation presentation{app::present_playback(playback_)};

    actions_.set_repeat(presentation.repeat);
    actions_.set_shuffled(presentation.shuffled);

    repeat_button_->setText(actions_.action(PlayerAction::repeat)->text());
    shuffle_button_->setChecked(actions_.action(PlayerAction::shuffle)->isChecked());
}

void MainWindow::update_after_pump(const app::PlayerPresentation& presentation)
{
    const engine::PlaybackSnapshot& playback = presentation.playback;
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
    if (playback.track != named_ || playback_.playlist().position() != named_place_) {
        named_ = playback.track;
        named_place_ = playback_.playlist().position();

        const QString name{named_.empty() ? QString{"no track"}
                                          : platform::qt::QtPathAdapter::to_qt(named_.filename())};

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
    const bool pausing{app::present_playback(playback_).playback.playing};
    const engine::CommandResult result{playback_.toggle_playback()};

    show_status(result == engine::CommandResult::accepted || pausing
            ? QString{}
            : QString{"no playback device"});
}

void MainWindow::stop_playback()
{
    if (!loaded())
        return;

    playback_.stop();
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
    playback_.seek_to(fraction);
}

void MainWindow::show_status(const QString& message)
{
    status_label_->setText(message);
}

void MainWindow::refresh()
{
    const engine::PlaybackSnapshot playback{app::present_playback(playback_).playback};

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
    now_playing_->show_playlist(playback_.playlist());

    const engine::Track* current = playback_.playlist().current();
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
