/**
 * @file
 * @brief Library navigation identity and selection regression tests.
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

#include <gui/library_navigation.hpp>

#include <library/in_memory_repository.hpp>

#include <QApplication>

#include <gtest/gtest.h>

#include <optional>
#include <utility>

namespace {

TEST(LibraryNavigation, ListsRepositoryPlaylistsAndTypedWorkspaces)
{
    wiola::library::InMemoryRepository repository;
    const auto road = repository.create_playlist("Road");
    const auto evening = repository.create_playlist("Evening");

    wiola::gui::LibraryNavigation navigation;
    navigation.show_repository(repository);
    navigation.show_workspaces({
        {.id = wiola::library::WorkspaceId{"workspace-music"}, .name = "Music"}
    });

    ASSERT_EQ(navigation.topLevelItemCount(), 2);

    QTreeWidgetItem* playlists = navigation.topLevelItem(0);
    QTreeWidgetItem* workspaces = navigation.topLevelItem(1);
    ASSERT_EQ(playlists->childCount(), 2);
    ASSERT_EQ(workspaces->childCount(), 1);

    EXPECT_EQ(playlists->text(0), "Playlists");
    EXPECT_EQ(playlists->child(0)->text(0), "Road");
    EXPECT_EQ(playlists->child(1)->text(0), "Evening");
    EXPECT_EQ(workspaces->text(0), "Workspaces");
    EXPECT_EQ(workspaces->child(0)->text(0), "Music");
    EXPECT_TRUE(playlists->isExpanded());
    EXPECT_TRUE(workspaces->isExpanded());

    std::optional<wiola::library::PlaylistId> chosen_playlist;
    std::optional<wiola::library::WorkspaceId> chosen_workspace;
    navigation.on_playlist_chosen([&chosen_playlist](wiola::library::PlaylistId id) {
        chosen_playlist = std::move(id);
    });
    navigation.on_workspace_chosen([&chosen_workspace](wiola::library::WorkspaceId id) {
        chosen_workspace = std::move(id);
    });

    navigation.setCurrentItem(playlists->child(1));
    EXPECT_EQ(chosen_playlist, evening);

    navigation.setCurrentItem(workspaces->child(0));
    EXPECT_EQ(chosen_workspace, wiola::library::WorkspaceId{"workspace-music"});
}

TEST(LibraryNavigation, RefreshesContentsWithoutReusingDisplayNamesAsIdentity)
{
    wiola::library::InMemoryRepository repository;
    const auto first = repository.create_playlist("Same name");
    const auto second = repository.create_playlist("Same name");

    wiola::gui::LibraryNavigation navigation;
    navigation.show_repository(repository);

    std::optional<wiola::library::PlaylistId> chosen;
    navigation.on_playlist_chosen([&chosen](wiola::library::PlaylistId id) {
        chosen = std::move(id);
    });

    QTreeWidgetItem* playlists = navigation.topLevelItem(0);
    navigation.setCurrentItem(playlists->child(1));
    EXPECT_EQ(chosen, second);
    EXPECT_NE(first, second);

    repository.rename_playlist(second, "Renamed");
    navigation.show_repository(repository);

    EXPECT_EQ(playlists->childCount(), 2);
    EXPECT_EQ(playlists->child(1)->text(0), "Renamed");
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
