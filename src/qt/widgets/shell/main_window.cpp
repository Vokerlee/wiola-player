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
#include "window_panels.hpp"

#include "document_host.hpp"
#include "library_navigation.hpp"
#include "player_actions.hpp"
#include "saved_playlist_manager.hpp"
#include <QApplication>
#include <app/playback/queue_settings.hpp>
#include <qt/application/library/library_session.hpp>
#include <qt/application/playback/playback_preferences.hpp>
#include <qt/widgets/appearance/theme_manager.hpp>
#include <qt/widgets/controls/equalizer_panel.hpp>
#include <qt/widgets/controls/status_view.hpp>
#include <qt/widgets/controls/transport_view.hpp>
#include <qt/widgets/controls/volume_view.hpp>
#include <qt/widgets/documents/now_playing_document.hpp>
#include <qt/widgets/documents/saved_playlist_document.hpp>
#include <qt/widgets/documents/workspace_document.hpp>
#include <qt/widgets/layout/layout_document.hpp>
#include <qt/widgets/layout/layout_session.hpp>
#include <qt/widgets/layout/view_registry.hpp>
#include <qt/widgets/metadata/track_inspector.hpp>
#include <wiola/app/playback/playback_service.hpp>
#include <wiola/app/playback/volume_control.hpp>
#include <wiola/library/metadata_patch.hpp>
#include <wiola/media/local/resource.hpp>

#include <app/metadata/custom_tag_commands.hpp>
#include <app/playback/playback_queue.hpp>
#include <qt/application/library/m3u_playlist.hpp>
#include <qt/application/metadata/media_revision_publisher.hpp>
#include <qt/application/workspace/workspace_watcher.hpp>
#include <wiola/app/library/playlist_destination.hpp>
#include <wiola/app/metadata/metadata_edit_service.hpp>
#include <wiola/app/metadata/metadata_merge.hpp>
#include <wiola/library/in_memory_repository.hpp>
#include <wiola/media/audio_decoder.hpp>
#include <wiola/units.hpp>

#include "document_command_router.hpp"
#include "player_dialogs.hpp"
#include "player_menu.hpp"
#include <qt/widgets/metadata/metadata_inspector_binding.hpp>

#include <qt/widgets/appearance/presentation_tokens.hpp>
#include <qt/widgets/appearance/theme.hpp>
#include <qt/widgets/appearance/theme_editor.hpp>
#include <qt/widgets/layout/classic_layout.hpp>
#include <qt/widgets/layout/editor/layout_editor_dialog.hpp>
#include <qt/widgets/layout/focused_layout.hpp>
#include <qt/widgets/layout/layout_builder.hpp>
#include <wiola/qt/paths.hpp>
#include <wiola/qt/storage/settings.hpp>

#include <wiola/library/metadata.hpp>
#include <wiola/library/records.hpp>

#include <QActionGroup>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QScreen>
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

    case engine::OpenResult::output_unavailable:
        return QString{"audio output is unavailable"};

    default:
        return QString{"cannot read that file"};
    }
}

} // namespace

bool MainWindow::loaded() const noexcept
{
    return app::present_playback(playback_).playback.loaded;
}

MainWindow::MainWindow(app::PlaybackService& playback_service, app::LibrarySession& library_session,
    app::PlaybackPreferences& preferences)
    // A file of its own on both platforms, rather than the registry on one of them.
    : playback_{playback_service}
    , actions_{*this}
    , settings_{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"}
    , ui_preferences_{preferences.ui_session()}
    , playlist_{preferences.queue()}
    , volume_{preferences.volume()}
    , equalizer_{preferences.equalizer()}
    , library_session_{library_session}
    , library_{library_session.repository()}
    , playlist_destination_{library_session.playlist_destination()}
{
    for (const auto& descriptor : action_descriptors())
        addAction(actions_.action(descriptor.id));
    ui_preferences_.bind_library(library_session_);
    theme_ = ThemeManager::acquire(settings_);
    actions_.reload_icons(*this);

    setWindowTitle("Wiola Player");
    setMinimumSize(tokens.player.window_min_width, tokens.player.window_min_height);

    transport_ = new TransportView{actions_, this};
    volume_view_ = new VolumeView{this};
    status_ = new StatusView{this};
    show_volume();
    if (!library_session_.restore_error().isEmpty())
        show_status(library_session_.restore_error());

    dialogs_ = std::make_unique<NativePlayerDialogs>(*this);
    commands_ = new DocumentCommandRouter{library_session_, playback_, playlist_, *dialogs_,
        [this](const DocumentCommandOutcome& outcome) {
            if (outcome.open_result)
                said_ = *outcome.open_result;
            if (outcome.open_playlist)
                static_cast<void>(document_host_->open_playlist(*outcome.open_playlist));
            if (outcome.open_workspace)
                static_cast<void>(document_host_->open_workspace(*outcome.open_workspace));
            if (!outcome.message.isEmpty())
                show_status(outcome.message);
            if (outcome.queue_changed)
                refresh();
        },
        this};
    DocumentCommands document_commands;
    document_commands.selection_changed = [this] {
        if (inspector_binding_)
            inspector_binding_->refresh();
    };
    document_commands.edit_track = [this] {
        if (inspector_binding_) {
            inspector_binding_->refresh();
            panels_->show_inspector();
            inspector_->edit_details();
        }
    };
    document_commands.open_folder = [this] {
        commands_->choose_folder();
    };
    document_commands.execute = [this](auto command, auto context) {
        commands_->execute(command, std::move(context));
    };
    document_commands.play_playlist = [this](const auto& id, auto entry) {
        commands_->play_saved(id, entry);
    };
    document_commands.rescan = [this](const auto& id, bool recursive) {
        commands_->rescan(id, recursive);
    };
    document_host_ = new DocumentHost{library_session_, playback_, ui_preferences_,
        std::move(document_commands), this};
    documents_ = &document_host_->tabs();
    now_playing_ = &document_host_->queue();
    now_playing_->show_library(library_);

    navigation_ = new LibraryNavigation{this};
    navigation_->show_repository(library_);
    show_workspace_navigation();
    playlist_manager_ =
        new SavedPlaylistManager{library_, library_session_.playlists(), *navigation_, this};
    inspector_ = new TrackInspector{this};

    inspector_binding_ = new MetadataInspectorBinding{*inspector_, *document_host_,
        library_session_, playback_, [status = QPointer<StatusView>{status_}](const QString& text) {
            if (status)
                status->show_message(text);
        }};

    connect(&library_session_, &app::LibrarySession::status_changed, this,
        &MainWindow::show_status);
    connect(&library_session_, &app::LibrarySession::workspace_changed, this,
        [this](const library::WorkspaceId& id) {
            if (library_.workspace(id))
                show_status("Folder changed; refreshing workspace");
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

    panels_ = std::make_unique<WindowPanels>(*this, *library_pane_, *inspector_,
        std::vector<QWidget*>{navigation_, playlist_manager_});

    navigation_->on_playlist_chosen([this](const auto& id) {
        static_cast<void>(document_host_->open_playlist(id));
    });
    navigation_->on_workspace_chosen([this](const auto& id) {
        static_cast<void>(document_host_->open_workspace(id));
    });

    transport_->register_views(views_);
    volume_view_->register_views(views_);
    status_->register_views(views_);
    static_cast<void>(views_.add(component_view("documentArea"), document_area_,
        LayoutComponentKind::splitter, true));
    static_cast<void>(views_.add(component_view("libraryPane"), library_pane_,
        LayoutComponentKind::column, true));
    static_cast<void>(views_.add(component_view("libraryNavigation"), navigation_,
        LayoutComponentKind::inspector, false));
    static_cast<void>(views_.add(component_view("savedPlaylistManager"), playlist_manager_,
        LayoutComponentKind::inspector, false));
    static_cast<void>(views_.add(component_view("documentTabs"), document_host_,
        LayoutComponentKind::document_host, false));
    static_cast<void>(views_.add(component_view("trackInspector"), inspector_,
        LayoutComponentKind::inspector, false));
    static_cast<void>(views_.add(DocumentViewReference{DocumentId::queue()}, now_playing_,
        LayoutComponentKind::track_table, false));
    const auto legacy_layout = settings_.value("layout/preset", "classic").toString();
    layout_session_ = std::make_unique<LayoutSession>(
        platform::qt::QtPathAdapter::from_qt(
            QFileInfo{settings_.fileName()}.dir().filePath("layout.json")
        ),
        layout_for(legacy_layout),
        parse_layout_choice(legacy_layout).value_or(LayoutChoice::classic)
    );
    if (layout_session_->choice() != LayoutChoice::custom) {
        const auto choice = layout_session_->choice();
        layout_session_->update_preset(layout_for(layout_choice_name(choice)));
    }
    if (!install_layout(layout_session_->document(), layout_session_->choice(), false)) {
        layout_session_->use_fallback(classic_layout_document(), LayoutChoice::classic);
        static_cast<void>(install_layout(layout_session_->document(), LayoutChoice::classic,
            false));
    }
    const auto panel_state = platform::qt::QtSettingsAdapter{settings_}.read_panel_state();
    if (!panel_state.isEmpty())
        restoreState(panel_state, 1);
    const auto geometry = ui_preferences_.state().window;
    if (geometry.isValid()) {
        auto* screen = QGuiApplication::screenAt(geometry.center());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (screen) {
            const auto available = screen->availableGeometry();
            setMinimumSize(minimumSize().boundedTo(available.size()));
            const auto size = geometry.size().expandedTo(minimumSize()).boundedTo(available.size());
            const QPoint top{
                std::clamp(geometry.x(), available.left(), available.right() - size.width() + 1),
                std::clamp(geometry.y(), available.top(), available.bottom() - size.height() + 1)};
            setGeometry(QRect{top, size});
        }
    }
    for (auto* splitter : findChildren<QSplitter*>())
        if (const auto found =
                ui_preferences_.state().splitters.find(splitter->objectName().toStdString());
            found != ui_preferences_.state().splitters.end() &&
            found->second.size() == static_cast<std::size_t>(splitter->count())) {
            QList<int> sizes;
            for (const auto size : found->second)
                sizes.push_back(size);
            splitter->setSizes(sizes);
        }
    connect(&ui_preferences_, &app::SessionPreferences::save_failed, this,
        [this](auto) { show_status("Could not save view preferences; changes remain pending"); });
    if (ui_preferences_.restore_error() != app::SessionStateError::none)
        show_status(ui_preferences_.restore_error() == app::SessionStateError::unsupported_version
                ? "View preferences use a newer version; the saved file was retained"
                : "Could not restore all view preferences; a usable session was retained");
    const auto selected_layout = layout_choice_name(layout_session_->choice());
    if (layout_session_->restore_error() != LayoutSessionError::none)
        show_status("Could not restore the saved layout; a usable layout was retained");

    connect(actions_.action(PlayerAction::open), &QAction::triggered, commands_,
        [commands = commands_] { commands->choose_tracks(); });
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
    connect(actions_.action(PlayerAction::add), &QAction::triggered, commands_,
        [commands = commands_] { commands->choose_tracks(true); });
    connect(actions_.action(PlayerAction::remove), &QAction::triggered, document_host_,
        [host = document_host_] { host->dispatch(TrackCommand::remove, DocumentId::queue()); });
    connect(actions_.action(PlayerAction::clear), &QAction::triggered, commands_,
        &DocumentCommandRouter::clear_queue);
    connect(actions_.action(PlayerAction::equalizer), &QAction::triggered, this,
        &MainWindow::show_equalizer);

    const auto menus = build_player_menus(*menuBar(), actions_, *commands_,
        {[commands = commands_] { commands->choose_folder(); },
            [commands = commands_] { commands->import_playlist(); },
            [this] {
                std::optional<library::PlaylistId> target;
                if (const auto document = document_host_->registry().active();
                    document && document->playlist())
                    target = *document->playlist();
                commands_->export_playlist(target);
            },
            [library = QPointer<app::LibrarySession>{&library_session_}] {
                if (library)
                    library->retry_save();
            }});
    auto* view = menus.view;
    panels_->add_menu_actions(*view);
    QAction* classic = view->addAction("Classic");
    QAction* library = view->addAction("Library Focus");
    QAction* queue = view->addAction("Queue Focus");
    auto* presets = new QActionGroup{this};
    layout_presets_ = presets;
    classic->setData("classic");
    library->setData("library-focus");
    queue->setData("queue-focus");
    presets->setExclusive(true);

    for (QAction* preset : {classic, library, queue}) {
        preset->setCheckable(true);
        presets->addAction(preset);
    }

    classic->setChecked(selected_layout == "classic");
    library->setChecked(selected_layout == "library-focus");
    queue->setChecked(selected_layout == "queue-focus");

    const auto choose_layout = [this](const QString& choice) {
        static_cast<void>(install_layout(layout_for(choice),
            parse_layout_choice(choice).value_or(LayoutChoice::classic), true));
        show_layout_choice();
    };

    connect(classic, &QAction::triggered, this, [choose_layout] { choose_layout("classic"); });
    connect(library, &QAction::triggered, this,
        [choose_layout] { choose_layout("library-focus"); });
    connect(queue, &QAction::triggered, this, [choose_layout] { choose_layout("queue-focus"); });

    view->addSeparator();
    QAction* light = view->addAction("Light theme");
    QAction* dark = view->addAction("Dark theme");
    QAction* custom = view->addAction("Custom theme");
    auto* themes = new QActionGroup{this};
    themes->setExclusive(true);
    for (auto* action : {light, dark, custom}) {
        action->setCheckable(true);
        themes->addAction(action);
    }
    const auto reflect_theme = [this, light, dark, custom] {
        actions_.reload_icons(*this);
        light->setChecked(theme_->state().selected == ThemeChoice::light);
        dark->setChecked(theme_->state().selected == ThemeChoice::dark);
        custom->setChecked(theme_->state().selected == ThemeChoice::custom);
    };
    reflect_theme();
    connect(theme_.get(), &ThemeManager::theme_changed, this,
        [reflect_theme](auto) { reflect_theme(); });
    const auto choose_theme = [this, reflect_theme](ThemeChoice choice) {
        const auto error = theme_->select(choice);
        if (error != ThemeError::none)
            show_status("Could not save the theme; the current theme was retained");
        reflect_theme();
    };
    connect(light, &QAction::triggered, this, [choose_theme] { choose_theme(ThemeChoice::light); });
    connect(dark, &QAction::triggered, this, [choose_theme] { choose_theme(ThemeChoice::dark); });
    connect(custom, &QAction::triggered, this,
        [choose_theme] { choose_theme(ThemeChoice::custom); });

    auto* tools = menus.tools;
    QAction* edit_layout = tools->addAction("Edit button layout...");
    connect(edit_layout, &QAction::triggered, this, [this] {
        auto* editor = new LayoutEditorDialog{layout_session_->document(), this};
        editor->setAttribute(Qt::WA_DeleteOnClose);
        editor->on_apply([owner = QPointer<MainWindow>{this}](const LayoutDocument& document) {
            return owner && owner->switch_layout(document);
        });
        editor->show();
    });

    auto* import_layout_action = tools->addAction("Import layout...");
    connect(import_layout_action, &QAction::triggered, this, [this] {
        const QPointer<MainWindow> alive{this};
        const auto path = dialogs_->file(PlayerFileChoice::import_layout);
        if (!alive || !path)
            return;
        persistence::qt::AtomicFileStore input{*path, 256 * 1024};
        const auto read = input.read(persistence::qt::FileSlot::primary);
        if (read.error != persistence::qt::FileStoreError::none || !read.exists)
            show_status("Could not read the layout file");
        else
            static_cast<void>(import_layout(read.bytes));
    });
    auto* export_layout_action = tools->addAction("Export layout...");
    connect(export_layout_action, &QAction::triggered, this, [this] {
        const QPointer<MainWindow> alive{this};
        const auto definition = layout_json();
        const auto path = dialogs_->file(PlayerFileChoice::export_layout);
        if (!alive || !path)
            return;
        persistence::qt::AtomicFileStore output{*path, 256 * 1024};
        show_status(output.write(persistence::qt::FileSlot::primary, definition) ==
                    persistence::qt::FileStoreError::none
                ? "Layout exported"
                : "Could not export the layout");
    });

    QAction* edit_theme = tools->addAction("Edit theme...");
    connect(edit_theme, &QAction::triggered, this, [this] {
        auto* editor = new ThemeEditor{theme_->colors(), this};
        editor->setAttribute(Qt::WA_DeleteOnClose);
        editor->on_apply([manager = theme_](const ThemeColors& colors) {
            return manager->set_custom(colors) == ThemeError::none;
        });
        editor->show();
    });
    auto* import_theme = tools->addAction("Import theme...");
    connect(import_theme, &QAction::triggered, this, [this] {
        const QPointer<MainWindow> alive{this};
        const auto path = dialogs_->file(PlayerFileChoice::import_theme);
        if (!alive || !path)
            return;
        persistence::qt::AtomicFileStore input{*path, 16 * 1024};
        const auto read = input.read(persistence::qt::FileSlot::primary);
        if (read.error != persistence::qt::FileStoreError::none || !read.exists ||
            theme_->import_json(read.bytes) != ThemeError::none)
            show_status("Could not import the theme; the current theme was retained");
    });
    auto* export_theme = tools->addAction("Export theme...");
    connect(export_theme, &QAction::triggered, this, [this] {
        const QPointer<MainWindow> alive{this};
        const auto definition = theme_->export_json();
        const auto path = dialogs_->file(PlayerFileChoice::export_theme);
        if (!alive || !path)
            return;
        persistence::qt::AtomicFileStore output{*path, 16 * 1024};
        show_status(output.write(persistence::qt::FileSlot::primary, definition) ==
                    persistence::qt::FileStoreError::none
                ? "Theme exported"
                : "Could not export the theme");
    });
    if (theme_->restore_error() != ThemeError::none)
        show_status("Could not restore the saved theme; a usable theme was retained");

    connect(volume_view_, &VolumeView::volume_requested, this, &MainWindow::set_volume);
    connect(volume_view_, &VolumeView::boost_requested, this, &MainWindow::set_boosted);
    connect(transport_, &TransportView::seek_requested, this, &MainWindow::seek_to);
    connect(documents_, &QTabWidget::currentChanged, this, &MainWindow::show_document_context);
    connect(&library_session_, &app::LibrarySession::active_destination_changed, this,
        &MainWindow::show_document_context);
    show_document_context();

    if (const std::size_t gone{preferences.missing_tracks()}; gone != 0)
        show_status(QString{"%1 queued track(s) are no longer where they were"}.arg(gone));

    // What was put back settles both of these, without either button having been pressed.
    show_list_buttons();
    library_changes_ = library_session_.repository().observe(
        [this](const library::LibraryChangeBatch& batch) { apply_library_changes(batch); }
    );

    refresh();
}

MainWindow::~MainWindow()
{
    save_window_state();
    document_host_->capture_preferences();
    QObject::disconnect(&ui_preferences_, nullptr, this, nullptr);
    QObject::disconnect(theme_.get(), nullptr, this, nullptr);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    save_window_state();
    document_host_->capture_preferences();
    QMainWindow::closeEvent(event);
}

void MainWindow::save_window_state()
{
    if (!layout_session_)
        return;
    platform::qt::QtSettingsAdapter{settings_}.write_panel_state(saveState(1));
    const auto geometry = this->geometry();
    std::map<std::string, std::vector<int>> splitters;
    for (auto* splitter : findChildren<QSplitter*>())
        if (!splitter->objectName().isEmpty()) {
            const auto sizes = splitter->sizes();
            splitters.emplace(splitter->objectName().toStdString(),
                std::vector<int>{sizes.begin(), sizes.end()});
        }
    ui_preferences_.edit([&](auto& state) {
        state.window = geometry;
        state.splitters = std::move(splitters);
        state.layout_identity = layout_choice_name(layout_session_->choice());
    });
}

void MainWindow::show_layout_choice()
{
    if (!layout_presets_ || !layout_session_)
        return;
    const auto key = layout_choice_name(layout_session_->choice());
    for (auto* action : layout_presets_->actions())
        action->setChecked(action->data().toString() == key);
}

bool MainWindow::switch_layout(const LayoutDocument& document)
{
    return install_layout(document, LayoutChoice::custom, true);
}

bool MainWindow::import_layout(QByteArrayView bytes)
{
    LayoutDocument document;
    if (read_layout_json(bytes, document) != LayoutJsonError::none) {
        show_status("That layout file is invalid or uses an unsupported version");
        return false;
    }
    return switch_layout(document);
}

QByteArray MainWindow::layout_json() const
{
    return write_layout_json(layout_session_->document());
}

bool MainWindow::install_layout(const LayoutDocument& document, LayoutChoice choice, bool persist)
{
    if (document.node_count == 0) {
        show_status("Could not construct the layout");
        return false;
    }

    const QPointer<QWidget> previous_focus = QApplication::focusWidget();
    ViewRegistry live = views_;
    const auto register_document = [&](DocumentId id, QWidget* widget) {
        static_cast<void>(live.add(DocumentViewReference{id}, widget,
            LayoutComponentKind::track_table));
        static_cast<void>(live.add(DocumentViewReference{id, DocumentViewPart::search},
            widget->findChild<QWidget*>("trackSearchPanel"), LayoutComponentKind::search));
        static_cast<void>(live.add(DocumentViewReference{id, DocumentViewPart::tags},
            widget->findChild<QWidget*>("tagFilterPanel"), LayoutComponentKind::tags));
    };
    // Queue whole is already a singleton registration; its nested browser parts are separate views.
    static_cast<void>(live.add(DocumentViewReference{DocumentId::queue(), DocumentViewPart::search},
        now_playing_->findChild<QWidget*>("trackSearchPanel"), LayoutComponentKind::search));
    static_cast<void>(live.add(DocumentViewReference{DocumentId::queue(), DocumentViewPart::tags},
        now_playing_->findChild<QWidget*>("tagFilterPanel"), LayoutComponentKind::tags));
    for (const auto& [id, view] : document_host_->registry().playlists())
        if (view)
            register_document(DocumentId::playlist(id), view);
    for (const auto& [id, view] : document_host_->registry().workspaces())
        if (view)
            register_document(DocumentId::workspace(id), view);
    LayoutBuilder builder{live};

    auto prepared = builder.prepare(document.root, {.actions = &actions_}, this);
    QWidget* root = nullptr;
    if (persist && prepared) {
        const auto result = layout_session_->accept(document, choice, std::move(prepared), this);
        root = result.root;
        if (result.error != LayoutSessionError::none) {
            show_status(result.error == LayoutSessionError::unsupported_version
                    ? "Saved layout uses a newer version"
                    : result.error == LayoutSessionError::rollback_failed
                    ? "Could not restore the previous saved layout; the current view layout was "
                      "retained"
                    : "Could not save or attach this layout");
            return false;
        }
        settings_.remove("layout/preset");
        settings_.sync();
    } else if (prepared)
        root = prepared->attach(this);

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

    panels_->reconcile(*root);
    if (choice != LayoutChoice::custom)
        panels_->focus_queue(choice == LayoutChoice::queue_focus);
    for (const auto& [id, registration] : live.entries()) {
        QWidget* widget = registration.widget.data();
        if (!widget || panels_->contains(*widget))
            continue;
        bool registered_ancestor = false;
        for (QWidget* ancestor = widget->parentWidget(); ancestor && ancestor != this;
            ancestor = ancestor->parentWidget())
            if (std::ranges::any_of(live.entries(),
                    [ancestor](const auto& entry) { return entry.second.widget == ancestor; })) {
                registered_ancestor = true;
                break;
            }
        if (!belongs_to_root(widget)) {
            if (!registered_ancestor)
                widget->setParent(this);
            if (!registered_ancestor || std::holds_alternative<ComponentViewId>(id))
                widget->hide();
        }
    }

    if (QLayout* layout = root->layout())
        layout->setSpacing(tokens.player.section_spacing);

    setCentralWidget(root);
    if (isVisible()) {
        root->show();
        if (previous_focus && belongs_to_root(previous_focus))
            previous_focus->setFocus(Qt::OtherFocusReason);
    }
    show_layout_choice();
    if (persist)
        save_window_state();
    return true;
}

void MainWindow::open(std::vector<std::filesystem::path> tracks)
{
    commands_->open(std::move(tracks));
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

void MainWindow::apply_library_changes(const library::LibraryChangeBatch& batch)
{
    if (!batch.playlists.empty() || !batch.removed_playlists.empty()) {
        navigation_->show_repository(library_);
        show_document_context();
    }
    if (!batch.workspaces.empty() || !batch.removed_workspaces.empty())
        show_workspace_navigation();
    if (!batch.media.empty() || !batch.removed_media.empty()) {
        const bool playback_changed =
            now_playing_->show_library_changes(playback_.playlist(), library_, batch);
        if (inspector_binding_) {
            if (playback_changed)
                inspector_binding_->refresh();
            else
                inspector_binding_->refresh(batch);
        }
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

void MainWindow::show_list_buttons()
{
    transport_->present(app::present_playback(playback_));
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
    }

    // The title follows what is playing, so a list moving on to the next track renames the
    // window without anyone asking it to.
    if (playback.track != named_ || playback.track.label() != named_.label() ||
        playback_.playlist().position() != named_place_) {
        named_ = playback.track;
        named_place_ = playback_.playlist().position();

        QString name{"no track"};
        if (!named_.empty()) {
            const auto path = media::local_path(named_);
            name = path ? platform::qt::QtPathAdapter::to_qt(path->filename())
                        : QString::fromStdString(named_.label());
        }

        setWindowTitle(named_.empty() ? QString{"Wiola Player"} : name);
        said_fault_ = false;

        // Where the queue stands has moved, which is worth keeping even if nothing was added.
        playlist_.save();
    }

    // A device that goes away is not a listener pressing stop, and is the one thing about
    // playback worth interrupting them for.
    const bool faulted{playback.state == engine::PlaybackState::faulted};

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
    volume_view_->present({volume_.position(), volume_.max_position(), volume_.boosted()});
}

void MainWindow::set_boosted(bool boosted)
{
    volume_.set_boosted(boosted);

    show_volume();
}

void MainWindow::seek_to(double fraction)
{
    playback_.seek_to(fraction);
}

void MainWindow::show_document_context()
{
    QString destination = "None selected";
    if (const auto id = playlist_destination_.id())
        if (const auto record = library_.playlist(*id))
            destination = QString::fromStdString(record->name);
    status_->show_context(documents_->tabText(documents_->currentIndex()), destination);
}

void MainWindow::show_status(const QString& message)
{
    status_->show_message(message);
}

void MainWindow::refresh()
{
    transport_->present(app::present_playback(playback_));
    if (now_playing_->show_playlist(playback_.playlist()) && inspector_binding_)
        inspector_binding_->refresh();
}

} // namespace wiola::gui
