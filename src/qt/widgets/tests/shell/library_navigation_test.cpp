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

#include <qt/widgets/appearance/theme.hpp>
#include <qt/widgets/shell/library_navigation.hpp>
#include <wiola/library/in_memory_repository.hpp>

#include <QApplication>
#include <QImage>
#include <QTest>
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

    ASSERT_EQ(navigation.topLevelItemCount(), 3);
    EXPECT_EQ(navigation.topLevelItem(0)->text(0), "Play queue");

    QTreeWidgetItem* playlists = navigation.topLevelItem(1);
    QTreeWidgetItem* workspaces = navigation.topLevelItem(2);

    ASSERT_EQ(playlists->childCount(), 2);
    ASSERT_EQ(workspaces->childCount(), 1);

    EXPECT_EQ(playlists->text(0), "Playlists");
    EXPECT_EQ(playlists->child(0)->text(0), "Road");
    EXPECT_EQ(playlists->child(1)->text(0), "Evening");
    EXPECT_EQ(workspaces->text(0), "Folders");
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

TEST(LibraryNavigation, QueueIsIndependentOfLibraryGroupsAndCanBeReopenedByMouseOrKeyboard)
{
    wiola::library::InMemoryRepository repository;
    const auto saved = repository.create_playlist("Saved");
    wiola::gui::LibraryNavigation navigation;
    navigation.show_repository(repository);
    navigation.show();

    auto* queue = navigation.topLevelItem(0);

    ASSERT_NE(queue, nullptr);
    EXPECT_EQ(queue->parent(), nullptr);
    EXPECT_EQ(queue->childCount(), 0);

    int requests{};
    navigation.on_queue_chosen([&] { ++requests; });
    navigation.topLevelItem(1)->setExpanded(false);
    navigation.topLevelItem(2)->setExpanded(false);

    const auto click = [&] {
        QTest::mouseClick(navigation.viewport(), Qt::LeftButton, Qt::NoModifier,
            navigation.visualItemRect(queue).center());
    };
    click();
    EXPECT_EQ(requests, 1);
    EXPECT_FALSE(navigation.selected_playlist());

    navigation.show_repository(repository);
    navigation.show_workspaces({});
    EXPECT_EQ(navigation.currentItem(), queue);
    EXPECT_EQ(navigation.topLevelItem(0), queue);
    EXPECT_EQ(requests, 1);

    click();
    EXPECT_EQ(requests, 2);

    navigation.setFocus();
    QTest::keyClick(&navigation, Qt::Key_Return);
    EXPECT_EQ(requests, 3);
    EXPECT_EQ(repository.playlists().size(), 1);
    EXPECT_NE(repository.playlist(saved), nullptr);
}

TEST(LibraryNavigation, LegacyPlaylistNameIsClearWithoutChangingIdentityOrUserRenames)
{
    wiola::library::InMemoryRepository repository;
    const wiola::library::PlaylistId id{"playlist-migrated-queue"};

    ASSERT_TRUE(repository.put_playlist({.id = id, .name = "Migrated queue"}));

    wiola::gui::LibraryNavigation navigation;
    navigation.show_repository(repository);
    ASSERT_TRUE(navigation.select_playlist(id));
    EXPECT_EQ(navigation.currentItem()->text(0), "My playlist");
    EXPECT_EQ(repository.playlist(id)->name, "Migrated queue");
    ASSERT_TRUE(repository.rename_playlist(id, "My own name"));

    navigation.show_repository(repository);
    ASSERT_TRUE(navigation.select_playlist(id));
    EXPECT_EQ(navigation.currentItem()->text(0), "My own name");
}

TEST(LibraryNavigation, ActiveMarkerIsIndependentOfSelectionAndSurvivesRepositoryRefresh)
{
    wiola::library::InMemoryRepository repository;
    const auto first = repository.create_playlist("First");
    const auto second = repository.create_playlist("Second");
    wiola::gui::LibraryNavigation navigation;
    navigation.setPalette(
        wiola::gui::theme_palette(wiola::gui::theme_colors(wiola::gui::Theme::light))
    );
    navigation.show_repository(repository);
    ASSERT_TRUE(navigation.select_playlist(first));

    int choices{};
    navigation.on_playlist_chosen([&](auto) { ++choices; });
    navigation.set_active_playlist(second);
    navigation.set_active_playlist(second);

    auto* entries = navigation.topLevelItem(1);

    EXPECT_EQ(entries->child(0)->text(0), "First");
    EXPECT_FALSE(entries->child(0)->font(0).bold());
    EXPECT_EQ(entries->child(1)->text(0), "Second");
    EXPECT_TRUE(entries->child(1)->font(0).bold());
    EXPECT_FALSE(entries->child(1)->icon(0).isNull());
    EXPECT_EQ(navigation.selected_playlist(), first);

    const auto previous = entries->child(1)->icon(0).pixmap(24, 24).toImage();
    navigation.setPalette(
        wiola::gui::theme_palette(wiola::gui::theme_colors(wiola::gui::Theme::dark))
    );
    navigation.set_active_playlist(second);

    EXPECT_NE(entries->child(1)->icon(0).pixmap(24, 24).toImage(), previous);
    EXPECT_EQ(navigation.selected_playlist(), first);

    ASSERT_TRUE(repository.rename_playlist(second, "Renamed"));

    navigation.show_repository(repository);
    EXPECT_EQ(entries->child(1)->text(0), "Renamed");
    EXPECT_EQ(navigation.selected_playlist(), first);
    EXPECT_EQ(choices, 0);

    navigation.set_active_playlist({});
    EXPECT_EQ(entries->child(1)->text(0), "Renamed");
    EXPECT_FALSE(entries->child(1)->font(0).bold());
    EXPECT_TRUE(entries->child(1)->icon(0).isNull());
    EXPECT_TRUE(entries->child(1)->toolTip(0).isEmpty());
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

    QTreeWidgetItem* playlists = navigation.topLevelItem(1);
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
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
#ifdef Q_OS_WIN
        qputenv("QT_QPA_PLATFORM", "windows");
#else
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    }

    testing::InitGoogleTest(&argc, argv);

    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
