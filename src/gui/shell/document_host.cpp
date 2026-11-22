/**
 * @file
 * @brief Document tabs, typed registration, restoration, and view lifetime.
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

#include "document_host.hpp"
#include <QLabel>
#include <QPushButton>
#include <QTabBar>
#include <QVBoxLayout>
#include <algorithm>
#include <gui/documents/now_playing_document.hpp>
#include <gui/documents/saved_playlist_document.hpp>
#include <gui/documents/workspace_document.hpp>
#include <gui/tracks/track_browser.hpp>
#include <library/location.hpp>
#include <platform/qt/qt_path_adapter.hpp>

namespace wiola::gui {

DocumentHost::DocumentHost(app::LibrarySession& library, app::PlaybackService& playback,
    DocumentCommands commands, QWidget* parent)
    : QTabWidget{parent}
    , library_{library}
    , playback_{playback}
    , commands_{std::move(commands)}
    , settings_{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"}
    , empty_{new QWidget{this}}
    , queue_{new NowPlayingDocument{this}}
{
    queue_->findChild<TrackBrowser*>()->table().set_tag_catalog(&library_.tag_keys());
    setObjectName("documentTabs");
    setDocumentMode(true);
    setTabsClosable(true);
    addTab(queue_, "Now Playing");
    registry_.add(*queue_, [this] {
        std::vector<SelectedTrack> selected;
        for (const auto id : queue_->chosen_tracks())
            if (const auto row = playback_.playlist().index_of(id))
                selected.push_back({library_.media_at(playback_.playlist().tracks()[*row].path()),
                    id, playback_.playlist().tracks()[*row].path()});
        return selected;
    });
    static_cast<void>(registry_.activate(DocumentId::queue()));
    auto* empty_layout = new QVBoxLayout{empty_};
    auto* explanation =
        new QLabel{"Open a folder to browse its tracks without changing Now Playing.", empty_};
    explanation->setAlignment(Qt::AlignCenter);
    explanation->setWordWrap(true);
    auto* open = new QPushButton{"Open folder...", empty_};
    open->setObjectName("openFolderFromDocuments");
    empty_layout->addStretch();
    empty_layout->addWidget(explanation);
    empty_layout->addWidget(open);
    empty_layout->addStretch();
    addTab(empty_, "Open Folder");
    for (int index : {0, 1}) {
        tabBar()->setTabButton(index, QTabBar::RightSide, nullptr);
        tabBar()->setTabButton(index, QTabBar::LeftSide, nullptr);
    }
    connect(open, &QPushButton::clicked, &lifetime_, [this] {
        if (commands_.open_folder)
            commands_.open_folder();
    });
    connect(this, &QTabWidget::currentChanged, &lifetime_, [this](int) {
        registry_.activate(currentWidget());
        save();
    });
    connect(this, &QTabWidget::tabCloseRequested, &lifetime_, [this](int index) {
        if (const auto id = registry_.identify(widget(index)))
            static_cast<void>(close(*id));
    });
    connect(&library_, &app::LibrarySession::scan_batch, &lifetime_,
        [this](const app::WorkspaceScanBatch& batch) {
            if (auto* document = registry_.workspace(batch.workspace))
                static_cast<void>(document->show_batch(batch, &library_.repository()));
        });
    changes_ = library_.repository().observe([this](const auto& batch) {
        refresh(batch);
        for (const auto& [id, document] : registry_.workspaces())
            for (const auto& change : batch.media)
                static_cast<void>(document->show_media_revision(library_.repository(),
                    change.media));
    });
    restore();
    restoring_ = false;
    save();
}

DocumentHost::~DocumentHost()
{
    save();
}

bool DocumentHost::activate(const DocumentId& id)
{
    if (!registry_.activate(id))
        return false;
    setCurrentWidget(registry_.view(id));
    return true;
}

bool DocumentHost::open_playlist(const library::PlaylistId& id)
{
    if (registry_.playlist(id))
        return activate(DocumentId::playlist(id));
    auto* document = new SavedPlaylistDocument{this};
    document->findChild<TrackBrowser*>()->table().set_tag_catalog(&library_.tag_keys());
    if (!document->show_playlist(library_.repository(), id)) {
        delete document;
        return false;
    }
    registry_.add(id, *document, [document] { return document->selection(); });
    document->set_active(library_.active_playlist().id() == id);
    document->on_active_requested([this, id] {
        if (commands_.activate_playlist)
            commands_.activate_playlist(id);
    });
    document->on_play_requested([this, id] {
        if (commands_.play_playlist)
            commands_.play_playlist(id);
    });
    document->on_apply_order_requested([this, document] {
        if (commands_.apply_order)
            commands_.apply_order(document->displayed_paths());
    });
    document->on_transfer_active_requested([this, id, document] {
        if (commands_.transfer_playlist)
            commands_.transfer_playlist(id, *document, false);
    });
    document->on_transfer_destination_requested([this, id, document] {
        if (commands_.transfer_playlist)
            commands_.transfer_playlist(id, *document, true);
    });
    document->on_transfer_move_requested([this, id, document] {
        if (commands_.move_playlist)
            commands_.move_playlist(id, *document);
    });
    addTab(document, QString::fromStdString(library_.repository().playlist(id)->name));
    return activate(DocumentId::playlist(id));
}

bool DocumentHost::open_workspace(const library::WorkspaceId& id)
{
    const auto* workspace = library_.repository().workspace(id);
    if (!workspace)
        return false;
    if (registry_.workspace(id))
        return activate(DocumentId::workspace(id));
    auto* document = new WorkspaceDocument{this};
    document->findChild<TrackBrowser*>()->table().set_tag_catalog(&library_.tag_keys());
    registry_.add(id, *document, [document] { return document->selection(); });
    document->on_rescan_requested([this, id](bool recursive) {
        if (commands_.rescan)
            commands_.rescan(id, recursive);
    });
    document->on_cancel_requested([this, document] {
        library_.cancel_workspace_scan();
        document->show_cancelled();
    });
    document->on_play_requested([this, document] {
        if (commands_.play_workspace)
            commands_.play_workspace(*document);
    });
    document->on_append_requested([this, document] {
        if (commands_.append_workspace)
            commands_.append_workspace(*document);
    });
    document->on_apply_order_requested([this, document] {
        if (commands_.apply_order)
            commands_.apply_order(document->displayed_paths());
    });
    document->on_transfer_active_requested([this, document] {
        if (commands_.transfer_workspace)
            commands_.transfer_workspace(*document, false);
    });
    document->on_transfer_destination_requested([this, document] {
        if (commands_.transfer_workspace)
            commands_.transfer_workspace(*document, true);
    });
    const auto name = workspace->root.filename();
    addTab(document, name.empty() ? "Workspace" : platform::qt::QtPathAdapter::to_qt(name));
    begin_scan(id);
    return activate(DocumentId::workspace(id));
}

void DocumentHost::begin_scan(const library::WorkspaceId& id)
{
    auto* document = registry_.workspace(id);
    const auto* workspace = library_.repository().workspace(id);
    if (!document || !workspace)
        return;
    const auto generation = library_.scan_workspace(id, playback_.playback_extensions());
    document->begin(*workspace, generation);
}

bool DocumentHost::close(const DocumentId& id)
{
    if (id.is_queue())
        return false;
    auto* document = registry_.view(id);
    if (!document)
        return false;
    registry_.remove(id);
    removeTab(indexOf(document));
    document->deleteLater();
    save();
    return true;
}

void DocumentHost::refresh_active_destination()
{
    for (const auto& [id, document] : registry_.playlists())
        document->set_active(library_.active_playlist().id() == id);
}

void DocumentHost::refresh(const library::LibraryChangeBatch& batch)
{
    for (const auto& [id, document] : registry_.playlists()) {
        const auto* playlist = library_.repository().playlist(id);
        if (!playlist) {
            static_cast<void>(close(DocumentId::playlist(id)));
            continue;
        }
        if (std::ranges::find(batch.playlists, id) != batch.playlists.end()) {
            static_cast<void>(document->show_playlist(library_.repository(), id));
            setTabText(indexOf(document), QString::fromStdString(playlist->name));
        } else {
            for (const auto& change : batch.media)
                static_cast<void>(document->show_media_revision(library_.repository(),
                    change.media));
        }
    }
    for (const auto& [id, document] : registry_.workspaces())
        if (!library_.repository().workspace(id))
            static_cast<void>(close(DocumentId::workspace(id)));
    refresh_active_destination();
}

void DocumentHost::restore()
{
    const auto playlists = settings_.value("documents/open_playlists").toStringList();
    const auto workspaces = settings_.value("documents/open_workspaces").toStringList();
    const auto kind = settings_.value("documents/current_kind").toString();
    const auto current = settings_.value("documents/current_id").toString().toStdString();
    const auto legacy_playlist =
        settings_.value("documents/current_playlist").toString().toStdString();
    const auto legacy_workspace =
        settings_.value("documents/current_workspace").toString().toStdString();
    for (const auto& id : playlists)
        static_cast<void>(open_playlist(library::PlaylistId{id.toStdString()}));
    for (const auto& id : workspaces)
        static_cast<void>(open_workspace(library::WorkspaceId{id.toStdString()}));
    if (kind == "empty") {
        setCurrentWidget(empty_);
        return;
    }
    if ((kind == "playlist" && activate(DocumentId::playlist(library::PlaylistId{current}))) ||
        (kind == "workspace" && activate(DocumentId::workspace(library::WorkspaceId{current}))))
        return;
    if (kind.isEmpty()) {
        if (!legacy_workspace.empty() &&
            activate(DocumentId::workspace(library::WorkspaceId{legacy_workspace})))
            return;
        if (!legacy_playlist.empty() &&
            activate(DocumentId::playlist(library::PlaylistId{legacy_playlist})))
            return;
    }
    static_cast<void>(activate(DocumentId::queue()));
}

void DocumentHost::save()
{
    if (restoring_)
        return;
    QStringList playlists, workspaces;
    for (const auto& [id, view] : registry_.playlists())
        playlists.push_back(QString::fromStdString(id.value));
    for (const auto& [id, view] : registry_.workspaces())
        workspaces.push_back(QString::fromStdString(id.value));
    const auto active = registry_.active();
    QString kind{"empty"}, value, playlist, workspace;
    if (active) {
        if (active->is_queue())
            kind = "queue";
        else if (active->playlist()) {
            kind = "playlist";
            value = playlist = QString::fromStdString(active->playlist()->value);
        } else if (active->workspace()) {
            kind = "workspace";
            value = workspace = QString::fromStdString(active->workspace()->value);
        }
    }
    settings_.setValue("documents/open_playlists", playlists);
    settings_.setValue("documents/open_workspaces", workspaces);
    settings_.setValue("documents/current_kind", kind);
    settings_.setValue("documents/current_id", value);
    settings_.setValue("documents/current_playlist", playlist);
    settings_.setValue("documents/current_workspace", workspace);
    settings_.sync();
}

} // namespace wiola::gui
