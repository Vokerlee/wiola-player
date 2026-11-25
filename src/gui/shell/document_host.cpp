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
#include <media/local/resource.hpp>
#include <qt/paths.hpp>

namespace wiola::gui {

DocumentHost::DocumentHost(app::LibrarySession& library, app::PlaybackService& playback,
    app::SessionPreferences& preferences, DocumentCommands commands, QWidget* parent)
    : QTabWidget{parent}
    , library_{library}
    , playback_{playback}
    , commands_{std::move(commands)}
    , preferences_{preferences}
    , empty_{new QWidget{this}}
    , queue_{new NowPlayingDocument{this}}
{
    configure_browser(queue_->browser());
    queue_->set_media_resolver([this](const std::filesystem::path& path) {
        return library_.media_at(path);
    });
    setObjectName("documentTabs");
    setDocumentMode(true);
    setTabsClosable(true);
    addTab(queue_, "Now Playing");
    registry_.add(*queue_, [this] {
        std::vector<SelectedTrack> selected;
        for (const auto id : queue_->chosen_tracks())
            if (const auto row = playback_.playlist().index_of(id)) {
                auto path = media::local_path(playback_.playlist().tracks()[*row].resource());
                selected.push_back({path ? library_.media_at(*path) : library::MediaId{}, id,
                    path ? std::move(*path) : std::filesystem::path{}});
            }
        return selected;
    });
    connect(queue_, &NowPlayingDocument::track_chosen, &lifetime_, [this](engine::QueueEntryId id) {
        auto context = capture(DocumentId::queue());
        context.selection = {
            {library::MediaId{}, id, {}}
        };
        if (commands_.execute)
            commands_.execute(TrackCommand::play, std::move(context));
    });
    connect(queue_, &NowPlayingDocument::removal_asked, &lifetime_,
        [this] { dispatch(TrackCommand::remove, DocumentId::queue()); });
    connect(queue_, &NowPlayingDocument::transfer_active_asked, &lifetime_,
        [this] { dispatch(TrackCommand::transfer, DocumentId::queue()); });
    connect(queue_, &NowPlayingDocument::transfer_destination_asked, &lifetime_,
        [this] { dispatch(TrackCommand::transfer_destination, DocumentId::queue()); });
    connect(queue_, &NowPlayingDocument::order_application_asked, &lifetime_,
        [this] { dispatch(TrackCommand::apply_order, DocumentId::queue()); });
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
        if (commands_.selection_changed)
            commands_.selection_changed();
    });
    connect(this, &QTabWidget::tabCloseRequested, &lifetime_, [this](int index) {
        if (const auto id = registry_.identify(widget(index)))
            static_cast<void>(close(*id));
    });
    connect(&library_, &app::LibrarySession::active_destination_changed, &lifetime_,
        [this] { refresh_active_destination(); });
    connect(&library_, &app::LibrarySession::workspace_scan_started, &lifetime_,
        [this](const auto& id, std::uint64_t generation) {
            if (auto* view = registry_.workspace(id))
                if (const auto* workspace = library_.repository().workspace(id))
                    view->begin(*workspace, generation);
        });
    connect(&library_, &app::LibrarySession::artwork_changed, &lifetime_,
        [this](const auto& media) {
            queue_->browser().table().refresh_artwork(media);
            for (const auto& [id, view] : registry_.playlists())
                if (view)
                    view->browser().table().refresh_artwork(media);
            for (const auto& [id, view] : registry_.workspaces())
                if (view)
                    view->browser().table().refresh_artwork(media);
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
    static_cast<void>(preferences_.flush());
}

void DocumentHost::configure_browser(TrackBrowser& browser)
{
    const QPointer<app::LibrarySession> artwork_library{&library_};
    browser.table().set_artwork_source(
        [artwork_library](const auto& id) {
            return artwork_library ? artwork_library->artwork(id) : media::ArtworkReference{};
        },
        [artwork_library](const auto& id) {
            if (artwork_library)
                artwork_library->request_artwork(id);
        }
    );
    browser.table().set_tag_catalog(&library_.tag_keys());
    browser.table().set_search_index(&library_.search());
    connect(&browser.table(), &TrackTableView::view_state_changed, &lifetime_, [this] { save(); });
    connect(&browser.table(), &TrackTableView::browser_preferences_changed, &lifetime_,
        [this] { save(); });
    connect(&browser, &TrackBrowser::selection_changed, &lifetime_, [this] {
        save();
        if (commands_.selection_changed)
            commands_.selection_changed();
    });
}

void DocumentHost::dispatch(TrackCommand command, const DocumentId& id)
{
    if (commands_.execute)
        commands_.execute(command, capture(id));
}

DocumentCommandContext DocumentHost::capture(const DocumentId& id) const
{
    DocumentCommandContext context{id, registry_.capabilities(id), registry_.selection(id), {}};
    if (id.is_queue())
        context.displayed_paths = queue_->displayed_paths(playback_.playlist());
    else if (const auto* playlist = id.playlist()) {
        if (const auto* view = registry_.playlist(*playlist))
            context.displayed_paths = view->displayed_paths();
    } else if (const auto* workspace = id.workspace()) {
        if (const auto* view = registry_.workspace(*workspace))
            context.displayed_paths = view->displayed_paths();
    }
    return context;
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
    configure_browser(document->browser());
    if (!document->show_playlist(library_.repository(), id)) {
        delete document;
        return false;
    }
    registry_.add(id, *document, [document] { return document->selection(); });
    document->set_active(library_.active_playlist().id() == id);
    document->on_active_requested([this, id] {
        dispatch(TrackCommand::activate_destination, DocumentId::playlist(id));
    });
    document->on_play_requested([this, id] {
        if (commands_.play_playlist)
            commands_.play_playlist(id, {});
    });
    document->on_entry_chosen([this, id](auto entry) {
        if (commands_.play_playlist)
            commands_.play_playlist(id, entry);
    });
    document->on_apply_order_requested([this, id] {
        dispatch(TrackCommand::apply_order, DocumentId::playlist(id));
    });
    document->on_transfer_active_requested([this, id] {
        dispatch(TrackCommand::transfer, DocumentId::playlist(id));
    });
    document->on_transfer_destination_requested([this, id] {
        dispatch(TrackCommand::transfer_destination, DocumentId::playlist(id));
    });
    document->on_transfer_move_requested([this, id] {
        dispatch(TrackCommand::move, DocumentId::playlist(id));
    });
    addTab(document, QString::fromStdString(library_.repository().playlist(id)->name));
    restore_browser(DocumentId::playlist(id), document->browser(), true);
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
    configure_browser(document->browser());
    registry_.add(id, *document, [document] { return document->selection(); });
    document->on_rescan_requested([this, id](bool recursive) {
        if (commands_.rescan)
            commands_.rescan(id, recursive);
    });
    document->on_cancel_requested([this, id, document] {
        library_.cancel_workspace_scan(id);
        document->show_cancelled();
    });
    document->on_play_requested([this, id] {
        dispatch(TrackCommand::play, DocumentId::workspace(id));
    });
    document->on_append_requested([this, id] {
        dispatch(TrackCommand::append, DocumentId::workspace(id));
    });
    document->on_apply_order_requested([this, id] {
        dispatch(TrackCommand::apply_order, DocumentId::workspace(id));
    });
    document->on_transfer_active_requested([this, id] {
        dispatch(TrackCommand::transfer, DocumentId::workspace(id));
    });
    document->on_transfer_destination_requested([this, id] {
        dispatch(TrackCommand::transfer_destination, DocumentId::workspace(id));
    });
    const auto name = workspace->root.filename();
    addTab(document, name.empty() ? "Workspace" : platform::qt::QtPathAdapter::to_qt(name));
    begin_scan(id);
    restore_browser(DocumentId::workspace(id), document->browser(), false);
    return activate(DocumentId::workspace(id));
}

void DocumentHost::begin_scan(const library::WorkspaceId& id)
{
    auto* document = registry_.workspace(id);
    const auto* workspace = library_.repository().workspace(id);
    if (!document || !workspace)
        return;
    static_cast<void>(library_.scan_workspace(id, playback_.playback_extensions()));
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
    if (catalog_revision_ != library_.tag_keys().revision()) {
        catalog_revision_ = library_.tag_keys().revision();
        queue_->browser().table().refresh_catalog();
        for (const auto& [id, document] : registry_.playlists())
            if (document)
                document->browser().table().refresh_catalog();
        for (const auto& [id, document] : registry_.workspaces())
            if (document)
                document->browser().table().refresh_catalog();
    }
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

void DocumentHost::restore_browser(const DocumentId& id, TrackBrowser& browser, bool complete)
{
    if (const auto found = preferences_.state().browsers.find(id);
        found != preferences_.state().browsers.end()) {
        const auto state = found->second;
        browser.restore_browser_state(state);
        if (complete)
            browser.table().finish_browser_restore();
    }
}

void DocumentHost::restore()
{
    const auto state = preferences_.state();
    for (const auto& id : state.open) {
        if (id.playlist())
            static_cast<void>(open_playlist(*id.playlist()));
        else if (id.workspace())
            static_cast<void>(open_workspace(*id.workspace()));
    }
    restore_browser(DocumentId::queue(), queue_->browser(), false);
    if (!state.visible) {
        setCurrentWidget(empty_);
        return;
    }
    if (!activate(*state.visible))
        static_cast<void>(activate(DocumentId::queue()));
}

void DocumentHost::save()
{
    if (restoring_)
        return;
    std::vector<DocumentId> open;
    std::map<DocumentId, app::BrowserViewState> browsers;
    for (int index = 0; index < count(); ++index)
        if (const auto id = registry_.identify(widget(index))) {
            open.push_back(*id);
            if (id->is_queue())
                browsers.emplace(*id, queue_->browser().capture_browser_state());
            else if (id->playlist())
                if (auto* view = registry_.playlist(*id->playlist()))
                    browsers.emplace(*id, view->browser().capture_browser_state());
            if (id->workspace())
                if (auto* view = registry_.workspace(*id->workspace()))
                    browsers.emplace(*id, view->browser().capture_browser_state());
        }
    const auto active = registry_.active();
    preferences_.edit([&](auto& state) {
        state.open = std::move(open);
        state.visible = active;
        for (auto& [id, value] : browsers)
            state.browsers.insert_or_assign(id, std::move(value));
        while (state.browsers.size() > 128) {
            auto oldest = std::ranges::find_if(state.browsers, [&](const auto& item) {
                return std::ranges::find(state.open, item.first) == state.open.end();
            });
            if (oldest == state.browsers.end())
                break;
            state.browsers.erase(oldest);
        }
    });
}

} // namespace wiola::gui
