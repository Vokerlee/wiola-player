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

#include <fixtures/track_view.hpp>

#include <qt/widgets/tracks/track_table_view.hpp>

#include <qt/widgets/documents/saved_playlist_document.hpp>

#include <app/library/local_identity.hpp>
#include <wiola/library/in_memory_repository.hpp>

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPersistentModelIndex>
#include <set>

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
    wiola::testing::set_track_sort(*table,
        {
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

TEST(SavedPlaylistDocument, CombinesTextTagSortingCountsAndPersistentSelection)
{
    wiola::library::InMemoryRepository repository;
    const auto make = [](std::string name, std::string title, std::string artist) {
        wiola::library::TrackRecord record;
        record.id = wiola::app::media_id_for(name + ".wav");
        record.embedded.title = std::move(title);
        record.embedded.artist = std::move(artist);
        record.embedded.album = "Shared album";
        record.locations = {name + ".wav"};
        record.tags = {
            {.id = wiola::library::TagId{name + "-tag"}, .key = "Genre", .value = "Jazz"}
        };
        return record;
    };

    const wiola::library::TrackRecord zulu = make("zulu", "Zulu", "Shared artist");
    const wiola::library::TrackRecord other = make("other", "Beta", "Other artist");
    const wiola::library::TrackRecord alpha = make("alpha", "Alpha", "Shared artist");
    ASSERT_TRUE(repository.put_track(zulu));
    ASSERT_TRUE(repository.put_track(other));
    ASSERT_TRUE(repository.put_track(alpha));

    const auto playlist = repository.create_playlist("Favorites");
    const auto zulu_entry = repository.append_track(playlist, zulu.id);
    const auto other_entry = repository.append_track(playlist, other.id);
    const auto alpha_entry = repository.append_track(playlist, alpha.id);
    ASSERT_TRUE(zulu_entry && other_entry && alpha_entry);

    wiola::gui::SavedPlaylistDocument document;
    ASSERT_TRUE(document.show_playlist(repository, playlist));
    wiola::gui::TrackTableView* table = document.findChild<wiola::gui::TrackTableView*>();

    document.findChild<QLineEdit*>("searchArtist")->setText("shared artist");
    document.findChild<QLineEdit*>("tagFilterKey")->setText("Genre");
    document.findChild<QLineEdit*>("tagFilterValue")->setText("Jazz");
    document.findChild<QPushButton*>("includeTagFilter")->click();
    wiola::testing::set_track_sort(*table,
        {
            wiola::gui::TrackSortKey{.field = wiola::gui::TrackSortField::title,
                                     .direction = wiola::gui::TrackSortDirection::ascending,
                                     .tag_key = {}}
    });

    QLabel* results = document.findChild<QLabel*>("trackSearchResults");
    ASSERT_NE(results, nullptr);
    EXPECT_EQ(results->text(), "2 of 3 tracks");
    ASSERT_EQ(table->model()->rowCount(), 2);
    EXPECT_EQ(table->model()->data(table->model()->index(0, 1), Qt::DisplayRole).toString(),
        "Alpha");

    table->selectionModel()->select(table->model()->index(0, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    EXPECT_EQ(document.chosen_entries(),
        std::vector<wiola::library::PlaylistEntryId>{*alpha_entry});

    document.findChild<QPushButton*>("resetTrackSearch")->click();
    EXPECT_EQ(results->text(), "3 of 3 tracks");
    EXPECT_EQ(document.chosen_entries(),
        std::vector<wiola::library::PlaylistEntryId>{*alpha_entry});

    const auto& entries = repository.playlist(playlist)->entries;
    ASSERT_EQ(entries.size(), 3);
    EXPECT_EQ(entries[0].id, *zulu_entry);
    EXPECT_EQ(entries[1].id, *other_entry);
    EXPECT_EQ(entries[2].id, *alpha_entry);
}

TEST(SavedPlaylistDocument, KeepsSelectedEntryIdentityAcrossMembershipChangesAndRename)
{
    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord track;
    track.id = {"opaque-media"};
    track.locations = {"/music/saved.flac"};
    ASSERT_TRUE(repository.put_track(track));
    const auto playlist = repository.create_playlist("Before");
    const auto first = repository.append_track(playlist, track.id);
    const auto selected = repository.append_track(playlist, track.id);
    ASSERT_TRUE(first && selected);
    wiola::gui::SavedPlaylistDocument document;
    ASSERT_TRUE(document.show_playlist(repository, playlist));
    auto* table = document.findChild<wiola::gui::TrackTableView*>();
    ASSERT_NE(table, nullptr);
    table->selectRow(1);
    ASSERT_EQ(document.chosen_entries(), std::vector{*selected});
    ASSERT_TRUE(repository.append_track(playlist, track.id));
    ASSERT_TRUE(document.show_playlist(repository, playlist));
    EXPECT_EQ(document.chosen_entries(), std::vector{*selected});
    ASSERT_TRUE(repository.remove_entry(playlist, *first));
    ASSERT_TRUE(repository.rename_playlist(playlist, "After"));
    ASSERT_TRUE(document.show_playlist(repository, playlist));
    EXPECT_EQ(document.chosen_entries(), std::vector{*selected});
    EXPECT_EQ(document.selection().front().media, track.id);
}

TEST(SavedPlaylistDocument, UpdatesEverySharedOccurrenceWithoutResettingUnrelatedRows)
{
    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord shared, other;
    shared.id = {"shared"};
    shared.locations = {"/music/shared.flac"};
    shared.embedded.title = "Before";
    other.id = {"other"};
    other.locations = {"/music/other.flac"};
    other.embedded.title = "Other";
    ASSERT_TRUE(repository.put_track(shared));
    ASSERT_TRUE(repository.put_track(other));
    const auto playlist = repository.create_playlist("Shared");
    ASSERT_TRUE(repository.append_track(playlist, shared.id));
    ASSERT_TRUE(repository.append_track(playlist, other.id));
    ASSERT_TRUE(repository.append_track(playlist, shared.id));
    wiola::gui::SavedPlaylistDocument document;
    ASSERT_TRUE(document.show_playlist(repository, playlist));
    auto* table = document.findChild<wiola::gui::TrackTableView*>();
    ASSERT_NE(table, nullptr);
    QPersistentModelIndex untouched{table->model()->index(1, 1)};
    int resets = 0;
    std::set<int> changed;
    QObject::connect(table->model(), &QAbstractItemModel::modelReset, [&] { ++resets; });
    QObject::connect(table->model(), &QAbstractItemModel::dataChanged,
        [&](const QModelIndex& first, const QModelIndex& last) {
            for (int row = first.row(); row <= last.row(); ++row)
                changed.insert(row);
        });
    shared.embedded.title = "After";
    ++shared.metadata_revision;
    ASSERT_TRUE(repository.put_track(shared));
    ASSERT_TRUE(document.show_media_revision(repository, shared.id));
    EXPECT_EQ(resets, 0);
    EXPECT_EQ(changed, (std::set<int>{0, 2}));
    EXPECT_TRUE(untouched.isValid());
    EXPECT_EQ(untouched.data().toString(), "Other");
    EXPECT_EQ(table->model()->index(0, 1).data().toString(), "After");
    EXPECT_EQ(table->model()->index(2, 1).data().toString(), "After");
}

TEST(SavedPlaylistDocument, SearchesTagsThroughTheCanonicalMediaIdentity)
{
    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord track;
    track.id = {"opaque-media-id"};
    track.locations = {"/music/not-the-media-id.flac"};
    track.embedded.artist = "Artist";
    track.tags = {
        {wiola::library::TagId{"tag"}, "Mood", "Calm"}
    };
    ASSERT_TRUE(repository.put_track(track));
    const auto playlist = repository.create_playlist("Tagged");
    ASSERT_TRUE(repository.append_track(playlist, track.id));
    ASSERT_TRUE(repository.append_track(playlist, track.id));
    wiola::gui::SavedPlaylistDocument document;
    ASSERT_TRUE(document.show_playlist(repository, playlist));
    auto* table = document.findChild<wiola::gui::TrackTableView*>();
    ASSERT_NE(table, nullptr);
    wiola::library::SearchQuery query;
    query.artist = "Artist";
    query.tags = "calm";
    wiola::testing::set_track_query(*table, query);
    EXPECT_EQ(table->visible_count(), 2);
    query.tags = "brisk";
    wiola::testing::set_track_query(*table, query);
    EXPECT_EQ(table->visible_count(), 0);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
