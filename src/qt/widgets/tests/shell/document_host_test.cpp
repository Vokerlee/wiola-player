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

#include <fakes/output.hpp>
#include <qt/widgets/documents/now_playing_document.hpp>
#include <qt/widgets/documents/saved_playlist_document.hpp>
#include <qt/widgets/documents/workspace_document.hpp>
#include <qt/widgets/layout/view_registry.hpp>
#include <qt/widgets/shell/document_host.hpp>
#include <qt/widgets/tracks/track_browser.hpp>
#include <qt/widgets/tracks/track_table_view.hpp>
#include <wiola/media/codecs/ffmpeg/composition.hpp>
#include <wiola/media/local/resource.hpp>
#include <wiola/qt/paths.hpp>

#include <QApplication>
#include <QEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <memory>

namespace {

using namespace wiola;

class DocumentHostTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(directory.isValid());

        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());

        QSettings settings{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"};
        preferences = std::make_unique<app::SessionPreferences>(settings);
        library = std::make_unique<app::LibrarySession>(wiola::codec::ffmpeg::make_provider());
    }

    QTemporaryDir directory;
    app::PlaybackService playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};

    std::unique_ptr<app::LibrarySession> library;
    std::unique_ptr<app::SessionPreferences> preferences;
};

TEST_F(DocumentHostTest,
    RestoresLastOpenedPlaylistIndependentlyOfTheVisibleQueueAndLegacyDestination)
{
    const auto first = library->playlists().create("First").id;
    const auto second = library->playlists().create("Second").id;

    ASSERT_TRUE(library->playlist_destination().set(second));

    preferences->edit([&](auto& state) {
        state.documents.open = {gui::DocumentId::queue(), gui::DocumentId::playlist(first),
            gui::DocumentId::playlist(second)};
        state.documents.visible = gui::DocumentId::queue();
        state.documents.last_playlist = first;
    });

    gui::DocumentHost host{*library, playback, *preferences, {}};

    EXPECT_EQ(preferences->state().documents.last_playlist, first);
    EXPECT_FALSE(host.active_playlist());
    EXPECT_EQ(library->playlist_destination().id(), second);
    ASSERT_TRUE(host.open_playlist(second));
    EXPECT_EQ(preferences->state().documents.last_playlist, second);
    ASSERT_TRUE(host.activate(gui::DocumentId::queue()));
    EXPECT_EQ(preferences->state().documents.last_playlist, second);
    ASSERT_TRUE(host.close(gui::DocumentId::playlist(second)));
    EXPECT_FALSE(preferences->state().documents.last_playlist);
}

TEST_F(DocumentHostTest, ActivatesExistingDocumentsAndClosesViewsWithoutDeletingRecords)
{
    const auto playlist = library->playlists().create("Favorites").id;
    gui::DocumentHost host{*library, playback, *preferences, {}};

    ASSERT_TRUE(host.open_playlist(playlist));

    auto* original = host.registry().playlist(playlist);

    ASSERT_NE(original, nullptr);
    EXPECT_TRUE(host.open_playlist(playlist));
    EXPECT_EQ(host.registry().playlist(playlist), original);
    EXPECT_EQ(host.tabs().count(), 2);
    EXPECT_TRUE(host.close(gui::DocumentId::queue()));
    EXPECT_EQ(host.indexOf(&host.queue()), -1);
    ASSERT_TRUE(host.activate(gui::DocumentId::queue()));
    EXPECT_EQ(host.count(), 2);
    ASSERT_TRUE(host.close(gui::DocumentId::playlist(playlist)));
    EXPECT_EQ(host.registry().playlist(playlist), nullptr);
    EXPECT_NE(library->repository().playlist(playlist), nullptr);
    ASSERT_TRUE(host.open_playlist(playlist));

    auto* replacement = host.registry().playlist(playlist);

    ASSERT_NE(replacement, nullptr);

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(host.registry().playlist(playlist), replacement);
    EXPECT_EQ(host.tabs().count(), 2);
}

TEST_F(DocumentHostTest, ClosingTheLastTabPreservesQueueRowsSelectionAndFiltersUntilReopened)
{
    playback.add({media::local_resource("queued.wav"), media::local_resource("queued.wav")});

    const auto entry = playback.playlist().tracks()[1].id();
    gui::DocumentHost host{*library, playback, *preferences, {}};
    auto& queue = host.queue();

    ASSERT_TRUE(queue.show_playlist(playback.playlist()));

    auto* search = queue.findChild<QLineEdit*>("searchTitle");

    ASSERT_NE(search, nullptr);

    search->setText("queued");
    queue.browser().table().selectRow(1);
    ASSERT_EQ(queue.chosen_tracks(), (std::vector<engine::QueueEntryId>{entry}));
    ASSERT_TRUE(host.close(gui::DocumentId::queue()));
    EXPECT_EQ(host.count(), 0);
    EXPECT_EQ(host.currentWidget(), nullptr);
    EXPECT_FALSE(host.registry().active());
    EXPECT_EQ(host.registry().view(gui::DocumentId::queue()), &queue);
    EXPECT_TRUE(queue.isHidden());
    EXPECT_FALSE(host.close(gui::DocumentId::queue()));

    host.refresh_playback();
    EXPECT_EQ(host.count(), 0);
    ASSERT_EQ(playback.playlist().tracks().size(), 2);
    EXPECT_EQ(playback.playlist().tracks()[1].id(), entry);
    ASSERT_TRUE(host.activate(gui::DocumentId::queue()));
    EXPECT_EQ(host.currentWidget(), &queue);
    EXPECT_EQ(host.count(), 1);
    EXPECT_EQ(search->text(), "queued");
    EXPECT_EQ(queue.chosen_tracks(), (std::vector<engine::QueueEntryId>{entry}));
    ASSERT_TRUE(host.activate(gui::DocumentId::queue()));
    EXPECT_EQ(host.count(), 1);
}

TEST_F(DocumentHostTest, RestoresTheActiveTypedDocumentWithoutChangingPlayback)
{
    const auto first = library->playlists().create("First").id;
    const auto second = library->playlists().create("Second").id;

    playback.add({wiola::media::local_resource("queued.wav"),
        wiola::media::local_resource("queued.wav")});

    const auto entry = playback.playlist().tracks()[1].id();
    {
        gui::DocumentHost host{*library, playback, *preferences, {}};

        ASSERT_TRUE(host.open_playlist(first));
        ASSERT_TRUE(host.open_playlist(second));
        ASSERT_TRUE(host.activate(gui::DocumentId::playlist(first)));
    }

    gui::DocumentHost restored{*library, playback, *preferences, {}};

    EXPECT_EQ(restored.tabs().count(), 3);
    EXPECT_EQ(restored.registry().active(), gui::DocumentId::playlist(first));
    EXPECT_EQ(restored.tabs().tabText(restored.tabs().currentIndex()), "First");
    ASSERT_EQ(playback.playlist().tracks().size(), 2);
    EXPECT_EQ(playback.playlist().tracks()[1].id(), entry);
}

TEST_F(DocumentHostTest, CommittedDeletionUpdatesTabsAndActiveIdentity)
{
    const auto playlist = library->playlists().create("Deleted").id;
    gui::DocumentHost host{*library, playback, *preferences, {}};

    ASSERT_TRUE(host.open_playlist(playlist));
    ASSERT_TRUE(library->playlists().remove(playlist));
    EXPECT_EQ(host.registry().playlist(playlist), nullptr);
    EXPECT_EQ(host.registry().active(), host.registry().identify(host.tabs().currentWidget()));
    EXPECT_EQ(host.tabs().count(), 1);
    EXPECT_FALSE(host.open_playlist(playlist));
}

TEST_F(DocumentHostTest, NoPermanentOpenFolderTabIsCreated)
{
    gui::DocumentHost host{*library, playback, *preferences, {}};

    EXPECT_EQ(host.count(), 1);
    EXPECT_EQ(host.currentWidget(), &host.queue());
    EXPECT_EQ(host.findChild<QPushButton*>("openFolderFromDocuments"), nullptr);
    EXPECT_TRUE(library->repository().workspaces().empty());
}

TEST_F(DocumentHostTest, WorkspaceAndPlaylistControlsCaptureCommandsFromTheirOwnDocument)
{
    library::MediaId media{"track"};
    library::WorkspaceId folder;
    library::PlaylistId playlist;
    {
        library::RepositoryTransaction transaction{library->repository(), library->persistence()};
        library::TrackRecord track;
        track.id = media;
        track.locations = {"song.wav"};
        ASSERT_TRUE(library->repository().put_track(track));
        folder = library->repository().create_workspace(directory.path().toStdString());
        playlist = library->repository().create_playlist("Saved");
        ASSERT_TRUE(library->repository().append_track(playlist, media));
        ASSERT_TRUE(transaction.commit());
    }
    std::vector<std::pair<gui::TrackCommand, gui::DocumentCommandContext>> commands;
    std::vector<bool> rescans;
    gui::DocumentCommands callbacks;
    callbacks.execute = [&](auto command, auto context) {
        commands.emplace_back(command, std::move(context));
    };
    callbacks.rescan = [&](const auto& id, bool recursive) {
        EXPECT_EQ(id, folder);
        rescans.push_back(recursive);
    };
    gui::DocumentHost host{*library, playback, *preferences, callbacks};
    EXPECT_FALSE(host.open_workspace(library::WorkspaceId{"absent"}));
    EXPECT_FALSE(host.activate(gui::DocumentId::playlist(library::PlaylistId{"absent"})));
    EXPECT_FALSE(host.close(gui::DocumentId::workspace(library::WorkspaceId{"absent"})));
    host.begin_scan(library::WorkspaceId{"absent"});
    ASSERT_TRUE(host.open_workspace(folder));
    auto* workspace = host.registry().workspace(folder);
    ASSERT_NE(workspace, nullptr);
    ASSERT_TRUE(host.open_workspace(folder));
    EXPECT_EQ(host.registry().workspace(folder), workspace);
    library->cancel_workspace_scan(folder);
    workspace->begin(*library->repository().workspace(folder), 99);
    app::WorkspaceScanBatch batch;
    batch.workspace = folder;
    batch.generation = 99;
    app::WorkspaceScanEntry entry;
    entry.media = media;
    entry.location = "song.wav";
    batch.entries = {entry};
    batch.status = app::WorkspaceScanStatus::complete;
    ASSERT_TRUE(workspace->show_batch(batch, &library->repository()));
    workspace->browser().table().selectRow(0);
    EXPECT_EQ(host.capture(gui::DocumentId::workspace(folder)).displayed_paths,
        (std::vector<std::filesystem::path>{"song.wav"}));

    workspace->findChild<QPushButton*>("rescanWorkspace")->click();
    EXPECT_EQ(rescans.size(), 1);
    workspace->findChild<QPushButton*>("playWorkspaceSelection")->click();
    workspace->findChild<QPushButton*>("appendWorkspaceSelection")->click();
    for (const auto command : {gui::TrackCommand::apply_order, gui::TrackCommand::transfer,
             gui::TrackCommand::transfer_destination})
        workspace->browser().command_requested(command);
    ASSERT_EQ(commands.size(), 5);
    for (const auto& [command, context] : commands)
        EXPECT_EQ(context.document, gui::DocumentId::workspace(folder));

    ASSERT_TRUE(host.open_playlist(playlist));
    auto* saved = host.registry().playlist(playlist);
    saved->browser().table().selectRow(0);
    for (const auto command : {gui::TrackCommand::apply_order, gui::TrackCommand::transfer,
             gui::TrackCommand::transfer_destination, gui::TrackCommand::move})
        saved->browser().command_requested(command);
    ASSERT_EQ(commands.size(), 9);
    for (std::size_t index = 5; index < commands.size(); ++index)
        EXPECT_EQ(commands[index].second.document, gui::DocumentId::playlist(playlist));

    playback.add({media::local_resource("queued.wav")});
    ASSERT_TRUE(host.queue().show_playlist(playback.playlist()));
    auto& queued = host.queue().browser().table();
    queued.selectRow(0);
    queued.activated(queued.model()->index(0, 0));
    EXPECT_EQ(commands.back().first, gui::TrackCommand::play);
    EXPECT_EQ(commands.back().second.document, gui::DocumentId::queue());

    workspace->begin(*library->repository().workspace(folder), 100);
    workspace->findChild<QPushButton*>("cancelWorkspaceScan")->click();
    EXPECT_FALSE(workspace->findChild<QPushButton*>("cancelWorkspaceScan")->isEnabled());

    {
        library::RepositoryTransaction transaction{library->repository(), library->persistence()};
        ASSERT_TRUE(library->repository().add_tag(media, "Mood", "Calm").valid());
        ASSERT_TRUE(library->repository().delete_workspace(folder));
        ASSERT_TRUE(transaction.commit());
    }
    EXPECT_EQ(host.registry().workspace(folder), nullptr);
}

TEST_F(DocumentHostTest, MissingVisibleDocumentFallsBackToQueueAndClosedBrowserHistoryIsBounded)
{
    preferences->edit([&](auto& state) {
        state.documents.visible = gui::DocumentId::workspace(library::WorkspaceId{"removed"});
        state.documents.last_playlist = library::PlaylistId{"removed"};
    });
    gui::DocumentHost host{*library, playback, *preferences, {}};
    EXPECT_EQ(host.registry().active(), gui::DocumentId::queue());
    EXPECT_FALSE(preferences->state().documents.last_playlist);
    preferences->edit([&](auto& state) {
        for (int i = 0; i < 130; ++i)
            state.documents.browsers.emplace(gui::DocumentId::playlist(library::PlaylistId{
                                                 std::to_string(i)}),
                gui::BrowserViewState{});
    });
    host.capture_preferences();
    EXPECT_EQ(preferences->state().documents.browsers.size(), 128);
    EXPECT_TRUE(preferences->state().documents.browsers.contains(gui::DocumentId::queue()));
}

TEST_F(DocumentHostTest, AnExplicitlyEmptyVisiblePreferenceRestoresTheTemporaryQueue)
{
    preferences->edit([](auto& state) { state.documents.visible.reset(); });
    gui::DocumentHost host{*library, playback, *preferences, {}};
    EXPECT_EQ(host.registry().active(), gui::DocumentId::queue());
    EXPECT_NE(host.indexOf(&host.queue()), -1);
}

TEST_F(DocumentHostTest, LayoutRegistrationFollowsOpenDocumentsAndOwnedBrowserParts)
{
    const auto playlist = library->playlists().create("Registered").id;
    const auto folder = library->repository().create_workspace(
        wiola::platform::qt::QtPathAdapter::from_qt(directory.path())
    );
    gui::DocumentHost host{*library, playback, *preferences, {}};
    ASSERT_TRUE(host.open_playlist(playlist));
    ASSERT_TRUE(host.open_workspace(folder));

    gui::ViewRegistry views;
    host.register_views(views);
    for (const auto& id : {gui::DocumentId::queue(), gui::DocumentId::playlist(playlist),
             gui::DocumentId::workspace(folder)}) {
        for (const auto part : {gui::DocumentViewPart::whole, gui::DocumentViewPart::search,
                 gui::DocumentViewPart::tags}) {
            const auto* registration = views.find(gui::DocumentViewReference{id, part});
            ASSERT_NE(registration, nullptr);
            EXPECT_FALSE(registration->widget.isNull());
        }
    }

    ASSERT_TRUE(host.close(gui::DocumentId::playlist(playlist)));
    gui::ViewRegistry remaining;
    host.register_views(remaining);
    EXPECT_EQ(remaining.find(gui::DocumentViewReference{gui::DocumentId::playlist(playlist)}),
        nullptr);
    EXPECT_NE(remaining.find(gui::DocumentViewReference{gui::DocumentId::workspace(folder)}),
        nullptr);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "minimal");
    ::testing::InitGoogleTest(&argc, argv);

    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
