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
#include <qt/widgets/shell/document_host.hpp>
#include <qt/widgets/tracks/track_browser.hpp>
#include <qt/widgets/tracks/track_table_view.hpp>
#include <wiola/media/codecs/ffmpeg/composition.hpp>
#include <wiola/media/local/resource.hpp>

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

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "minimal");
    ::testing::InitGoogleTest(&argc, argv);

    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
