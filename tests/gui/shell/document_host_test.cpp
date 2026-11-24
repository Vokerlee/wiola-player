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

#include <QApplication>
#include <QEvent>
#include <QPushButton>
#include <QTemporaryDir>
#include <codec/provider.hpp>
#include <fakes/output.hpp>
#include <gtest/gtest.h>
#include <gui/documents/saved_playlist_document.hpp>
#include <gui/shell/document_host.hpp>
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
        library =
            std::make_unique<app::LibrarySession>(std::make_shared<codec::LocalMediaProvider>());
    }

    QTemporaryDir directory;
    app::PlaybackService playback{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    std::unique_ptr<app::LibrarySession> library;
    std::unique_ptr<app::SessionPreferences> preferences;
};

TEST_F(DocumentHostTest, ActivatesExistingDocumentsAndClosesViewsWithoutDeletingRecords)
{
    const auto playlist = library->playlists().create("Favorites");
    gui::DocumentHost host{*library, playback, *preferences, {}};
    ASSERT_TRUE(host.open_playlist(playlist));
    auto* original = host.registry().playlist(playlist);
    ASSERT_NE(original, nullptr);
    EXPECT_TRUE(host.open_playlist(playlist));
    EXPECT_EQ(host.registry().playlist(playlist), original);
    EXPECT_EQ(host.tabs().count(), 3);
    EXPECT_FALSE(host.close(gui::DocumentId::queue()));
    ASSERT_TRUE(host.close(gui::DocumentId::playlist(playlist)));
    EXPECT_EQ(host.registry().playlist(playlist), nullptr);
    EXPECT_NE(library->repository().playlist(playlist), nullptr);
    ASSERT_TRUE(host.open_playlist(playlist));
    auto* replacement = host.registry().playlist(playlist);
    ASSERT_NE(replacement, nullptr);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(host.registry().playlist(playlist), replacement);
    EXPECT_EQ(host.tabs().count(), 3);
}

TEST_F(DocumentHostTest, RestoresTheActiveTypedDocumentWithoutChangingPlayback)
{
    const auto first = library->playlists().create("First");
    const auto second = library->playlists().create("Second");
    playback.add({"queued.wav", "queued.wav"});
    const auto entry = playback.playlist().tracks()[1].id();
    {
        gui::DocumentHost host{*library, playback, *preferences, {}};
        ASSERT_TRUE(host.open_playlist(first));
        ASSERT_TRUE(host.open_playlist(second));
        ASSERT_TRUE(host.activate(gui::DocumentId::playlist(first)));
    }
    gui::DocumentHost restored{*library, playback, *preferences, {}};
    EXPECT_EQ(restored.tabs().count(), 4);
    EXPECT_EQ(restored.registry().active(), gui::DocumentId::playlist(first));
    EXPECT_EQ(restored.tabs().tabText(restored.tabs().currentIndex()), "First");
    ASSERT_EQ(playback.playlist().tracks().size(), 2);
    EXPECT_EQ(playback.playlist().tracks()[1].id(), entry);
}

TEST_F(DocumentHostTest, CommittedDeletionUpdatesTabsAndActiveIdentity)
{
    const auto playlist = library->playlists().create("Deleted");
    gui::DocumentHost host{*library, playback, *preferences, {}};
    ASSERT_TRUE(host.open_playlist(playlist));
    ASSERT_TRUE(library->playlists().remove(playlist));
    EXPECT_EQ(host.registry().playlist(playlist), nullptr);
    EXPECT_EQ(host.registry().active(), host.registry().identify(host.tabs().currentWidget()));
    EXPECT_EQ(host.tabs().count(), 2);
    EXPECT_FALSE(host.open_playlist(playlist));
}

TEST_F(DocumentHostTest, EmptyStateRequestsOpenFolderWithoutCreatingAWorkspace)
{
    int requests = 0;
    gui::DocumentCommands commands;
    commands.open_folder = [&] {
        ++requests;
    };
    gui::DocumentHost host{*library, playback, *preferences, std::move(commands)};
    auto* open = host.findChild<QPushButton*>("openFolderFromDocuments");
    ASSERT_NE(open, nullptr);
    open->click();
    EXPECT_EQ(requests, 1);
    EXPECT_TRUE(library->repository().workspaces().empty());
    EXPECT_TRUE(host.registry().workspaces().empty());
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "minimal");
    ::testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};
    return RUN_ALL_TESTS();
}
