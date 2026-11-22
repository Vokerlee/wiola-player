/**
 * @file
 * @brief Weak typed document registration, active identity, and value selections.
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
#include <gtest/gtest.h>
#include <gui/documents/document_registry.hpp>
#include <gui/documents/now_playing_document.hpp>
#include <gui/documents/saved_playlist_document.hpp>
#include <gui/documents/workspace_document.hpp>
#include <memory>

namespace {

using namespace wiola::gui;
using wiola::library::PlaylistId;
using wiola::library::WorkspaceId;

TEST(DocumentRegistry, SeparatesTypedIdentitiesAndDocumentCapabilities)
{
    DocumentRegistry registry;
    NowPlayingDocument queue;
    SavedPlaylistDocument saved;
    WorkspaceDocument folder;
    const PlaylistId playlist{"same"};
    const WorkspaceId workspace{"same"};
    ASSERT_TRUE(registry.add(queue, [] { return std::vector<SelectedTrack>{}; }));
    ASSERT_TRUE(registry.add(playlist, saved, [] { return std::vector<SelectedTrack>{}; }));
    ASSERT_TRUE(registry.add(workspace, folder, [] { return std::vector<SelectedTrack>{}; }));
    EXPECT_NE(DocumentId::playlist(playlist), DocumentId::workspace(workspace));
    EXPECT_EQ(registry.view(DocumentId::playlist(playlist)), &saved);
    EXPECT_EQ(registry.view(DocumentId::workspace(workspace)), &folder);
    EXPECT_TRUE(registry.capabilities(DocumentId::playlist(playlist)).supports(TrackCommand::move));
    EXPECT_FALSE(
        registry.capabilities(DocumentId::workspace(workspace)).supports(TrackCommand::move)
    );
    EXPECT_TRUE(registry.capabilities(DocumentId::queue()).supports(TrackCommand::remove));
    EXPECT_FALSE(registry.add(PlaylistId{}, saved, [] { return std::vector<SelectedTrack>{}; }));
}

TEST(DocumentRegistry, RejectsDuplicateViewsAndReturnsOwnedOccurrenceSelections)
{
    DocumentRegistry registry;
    SavedPlaylistDocument first;
    SavedPlaylistDocument duplicate;
    const PlaylistId playlist{"saved"};
    std::vector<SelectedTrack> selected{
        {wiola::library::MediaId{"media"}, wiola::library::PlaylistEntryId{"entry"}}
    };
    ASSERT_TRUE(registry.add(playlist, first, [&] { return selected; }));
    EXPECT_FALSE(registry.add(playlist, duplicate, [] { return std::vector<SelectedTrack>{}; }));
    ASSERT_TRUE(registry.activate(DocumentId::playlist(playlist)));
    const auto snapshot = registry.selection();
    selected.clear();
    ASSERT_EQ(snapshot.size(), 1);
    EXPECT_EQ(std::get<wiola::library::PlaylistEntryId>(snapshot.front().occurrence).value,
        "entry");
    EXPECT_EQ(registry.view(*registry.active()), &first);
    EXPECT_TRUE(registry.selection().empty());
}

TEST(DocumentRegistry, DelayedDestructionCannotUnregisterAReplacementView)
{
    DocumentRegistry registry;
    auto* original = new SavedPlaylistDocument;
    const PlaylistId playlist{"saved"};
    ASSERT_TRUE(registry.add(playlist, *original, [] { return std::vector<SelectedTrack>{}; }));
    ASSERT_TRUE(registry.remove(DocumentId::playlist(playlist)));
    original->deleteLater();
    SavedPlaylistDocument replacement;
    ASSERT_TRUE(registry.add(playlist, replacement, [] { return std::vector<SelectedTrack>{}; }));
    ASSERT_TRUE(registry.activate(DocumentId::playlist(playlist)));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(registry.playlist(playlist), &replacement);
    EXPECT_EQ(registry.active(), DocumentId::playlist(playlist));
}

TEST(DocumentRegistry, DestroyedViewsAndRegistriesReleaseTheirObservers)
{
    auto saved = std::make_unique<SavedPlaylistDocument>();
    {
        DocumentRegistry registry;
        NowPlayingDocument queue;
        const PlaylistId playlist{"saved"};
        ASSERT_TRUE(registry.add(queue, [] { return std::vector<SelectedTrack>{}; }));
        ASSERT_TRUE(registry.add(playlist, *saved, [] { return std::vector<SelectedTrack>{}; }));
        ASSERT_TRUE(registry.activate(DocumentId::playlist(playlist)));
        saved.reset();
        EXPECT_EQ(registry.playlist(playlist), nullptr);
        EXPECT_EQ(registry.active(), DocumentId::queue());
        saved = std::make_unique<SavedPlaylistDocument>();
        ASSERT_TRUE(registry.add(playlist, *saved, [] { return std::vector<SelectedTrack>{}; }));
    }
    saved.reset();
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "minimal");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};
    return RUN_ALL_TESTS();
}
