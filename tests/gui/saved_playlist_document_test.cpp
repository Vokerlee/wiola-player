/**
 * @file
 * @brief Saved playlist document identity-mapping regression tests.
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

#include <gui/saved_playlist_document.hpp>

#include <library/in_memory_repository.hpp>

#include <QAction>
#include <QApplication>
#include <QLabel>

#include <gtest/gtest.h>

#include <optional>

namespace {

TEST(SavedPlaylistDocument, PresentsSavedRowsAndReturnsPersistentEntryIdentities)
{
    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord media;
    media.id = wiola::library::MediaId{"media-1"};
    media.embedded.title = "Embedded";
    media.embedded.artist = "Artist";
    media.overrides.title = "Override";
    media.locations = {"/music/track.flac"};
    ASSERT_TRUE(repository.put_track(media));

    const auto playlist = repository.create_playlist("Favorites");
    const auto first = repository.append_track(playlist, media.id);
    const auto duplicate = repository.append_track(playlist, media.id);
    ASSERT_TRUE(first && duplicate);

    wiola::gui::SavedPlaylistDocument document;
    ASSERT_TRUE(document.show_playlist(repository, playlist));

    wiola::gui::TrackTableView* table = document.findChild<wiola::gui::TrackTableView*>();
    QLabel* heading = document.findChild<QLabel*>("savedPlaylistHeading");
    ASSERT_NE(table, nullptr);
    ASSERT_NE(heading, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 2);

    EXPECT_EQ(heading->text(), "Favorites");
    EXPECT_EQ(table->model()->data(table->model()->index(0, 1), Qt::DisplayRole).toString(),
        "Override");
    EXPECT_EQ(table->model()->data(table->model()->index(0, 2), Qt::DisplayRole).toString(),
        "Artist");

    table->selectionModel()->select(table->model()->index(1, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    table->selectionModel()->setCurrentIndex(table->model()->index(1, 0),
        QItemSelectionModel::NoUpdate);

    EXPECT_EQ(document.chosen_entries(), std::vector<wiola::library::PlaylistEntryId>{*duplicate});

    std::optional<wiola::library::PlaylistEntryId> chosen;
    document.on_entry_chosen([&chosen](wiola::library::PlaylistEntryId id) {
        chosen = std::move(id);
    });

    const QList<QAction*> actions = table->actions();
    ASSERT_FALSE(actions.empty());
    actions.front()->trigger();

    EXPECT_EQ(chosen, duplicate);

    int play_requests{0};
    document.on_play_requested([&play_requests] { ++play_requests; });
    QPushButton* play_saved = document.findChild<QPushButton*>("playSavedPlaylist");
    ASSERT_NE(play_saved, nullptr);
    play_saved->click();

    EXPECT_EQ(play_requests, 1);

    int active_requests{0};
    document.on_active_requested([&active_requests] { ++active_requests; });
    QPushButton* use_active = document.findChild<QPushButton*>("useActivePlaylist");
    ASSERT_NE(use_active, nullptr);
    use_active->click();
    document.set_active(true);

    EXPECT_EQ(active_requests, 1);
    EXPECT_EQ(heading->text(), "Favorites — Active destination");

    int moves{0};
    document.on_transfer_move_requested([&moves] { ++moves; });
    const auto move = std::ranges::find_if(actions,
        [](QAction* candidate) { return candidate->text() == "Move to playlist →"; });
    ASSERT_NE(move, actions.end());
    (*move)->trigger();
    EXPECT_EQ(moves, 1);
}

TEST(SavedPlaylistDocument, RefreshesMetadataWithoutLosingPersistentSelection)
{
    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord media;
    media.id = wiola::library::MediaId{"media-1"};
    media.embedded.title = "Before";
    ASSERT_TRUE(repository.put_track(media));

    const auto playlist = repository.create_playlist("Favorites");
    const auto entry = repository.append_track(playlist, media.id);
    ASSERT_TRUE(entry);

    wiola::gui::SavedPlaylistDocument document;
    ASSERT_TRUE(document.show_playlist(repository, playlist));

    wiola::gui::TrackTableView* table = document.findChild<wiola::gui::TrackTableView*>();
    table->selectionModel()->select(table->model()->index(0, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);

    media.embedded.title = "After";
    ASSERT_TRUE(repository.put_track(media));
    ASSERT_TRUE(document.show_playlist(repository, playlist));

    EXPECT_EQ(table->model()->data(table->model()->index(0, 1), Qt::DisplayRole).toString(),
        "After");
    EXPECT_EQ(document.chosen_entries(), std::vector<wiola::library::PlaylistEntryId>{*entry});
    EXPECT_FALSE(document.show_playlist(repository, wiola::library::PlaylistId{"missing"}));
}

TEST(SavedPlaylistDocument, SortingPreservesPersistentEntrySelection)
{
    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord later;
    later.id = wiola::library::MediaId{"later"};
    later.embedded.title = "Zulu";
    later.locations = {"later.wav"};

    wiola::library::TrackRecord earlier;
    earlier.id = wiola::library::MediaId{"earlier"};
    earlier.embedded.title = "Alpha";
    earlier.locations = {"earlier.wav"};
    ASSERT_TRUE(repository.put_track(later));
    ASSERT_TRUE(repository.put_track(earlier));

    const auto playlist = repository.create_playlist("Favorites");
    const auto zulu = repository.append_track(playlist, later.id);
    const auto alpha = repository.append_track(playlist, earlier.id);
    ASSERT_TRUE(zulu && alpha);

    wiola::gui::SavedPlaylistDocument document;
    ASSERT_TRUE(document.show_playlist(repository, playlist));
    wiola::gui::TrackTableView* table = document.findChild<wiola::gui::TrackTableView*>();
    table->sort_by({
        wiola::gui::TrackSortKey{.field = wiola::gui::TrackSortField::title,
                                 .direction = wiola::gui::TrackSortDirection::ascending,
                                 .tag_key = {}}
    });

    ASSERT_EQ(table->model()->data(table->model()->index(0, 1), Qt::DisplayRole).toString(),
        "Alpha");
    table->selectionModel()->select(table->model()->index(0, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    EXPECT_EQ(document.chosen_entries(), std::vector<wiola::library::PlaylistEntryId>{*alpha});

    ASSERT_TRUE(document.show_playlist(repository, playlist));
    EXPECT_EQ(table->model()->data(table->model()->index(0, 1), Qt::DisplayRole).toString(),
        "Alpha");
    EXPECT_EQ(document.chosen_entries(), std::vector<wiola::library::PlaylistEntryId>{*alpha});

    int applications = 0;
    document.on_apply_order_requested([&] {
        ++applications;
        EXPECT_EQ(document.displayed_paths(),
            (std::vector<std::filesystem::path>{"earlier.wav", "later.wav"}));
    });

    for (QAction* candidate : table->actions()) {
        if (candidate->text() == "Apply displayed order to playback")
            candidate->trigger();
    }

    EXPECT_EQ(applications, 1);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
