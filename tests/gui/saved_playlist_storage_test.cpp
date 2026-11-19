/**
 * @file
 * @brief End-to-end saved playlist persistence regression tests.
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

#include <gui/saved_playlist_storage.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace {

class SavedPlaylistStorage : public testing::Test {
protected:
    std::filesystem::path file() const
    {
        return std::filesystem::path{directory.filePath("library.json").toStdString()};
    }

    QTemporaryDir directory;
};

wiola::library::TrackRecord track(const wiola::library::MediaId& id)
{
    wiola::library::TrackRecord record;
    record.id = id;

    return record;
}

TEST_F(SavedPlaylistStorage, RestoresOrderedEntriesAndDuplicateMediaWithStableIdentities)
{
    wiola::library::InMemoryRepository original;
    const wiola::library::MediaId first{"media-first"};
    const wiola::library::MediaId second{"media-second"};
    ASSERT_TRUE(original.put_track(track(first)));
    ASSERT_TRUE(original.put_track(track(second)));
    const auto workspace = original.create_workspace("music", false);
    ASSERT_TRUE(workspace.valid());

    const auto playlist = original.create_playlist("Favorites");
    const auto first_entry = original.append_track(playlist, first);
    const auto duplicate = original.append_track(playlist, first);
    const auto last_entry = original.append_track(playlist, second);
    ASSERT_TRUE(first_entry && duplicate && last_entry);

    wiola::gui::SavedPlaylistStorage storage{file()};
    ASSERT_EQ(storage.save(original), wiola::gui::LibraryJsonError::none);

    wiola::library::InMemoryRepository restored;
    ASSERT_TRUE(restored.put_track(track(wiola::library::MediaId{"obsolete"})));
    ASSERT_EQ(storage.restore(restored), wiola::gui::LibraryJsonError::none);

    EXPECT_EQ(restored.tracks().size(), 2);
    EXPECT_EQ(restored.track(wiola::library::MediaId{"obsolete"}), nullptr);
    ASSERT_NE(restored.workspace(workspace), nullptr);
    EXPECT_FALSE(restored.workspace(workspace)->recursive);

    const wiola::library::PlaylistRecord* saved = restored.playlist(playlist);
    ASSERT_NE(saved, nullptr);
    ASSERT_EQ(saved->entries.size(), 3);
    EXPECT_EQ(saved->name, "Favorites");
    EXPECT_EQ(saved->entries[0].id, *first_entry);
    EXPECT_EQ(saved->entries[1].id, *duplicate);
    EXPECT_EQ(saved->entries[2].id, *last_entry);
    EXPECT_EQ(saved->entries[0].media, first);
    EXPECT_EQ(saved->entries[1].media, first);
    EXPECT_EQ(saved->entries[2].media, second);
    EXPECT_EQ(restored.create_playlist("Next").value, "playlist-2");
}

TEST_F(SavedPlaylistStorage, RejectsInvalidSavedRecordsWithoutChangingTheRepository)
{
    wiola::library::InMemoryRepository repository;
    const wiola::library::MediaId retained{"retained"};
    ASSERT_TRUE(repository.put_track(track(retained)));

    const wiola::library::PlaylistRecord invalid{
        .id = wiola::library::PlaylistId{"playlist-invalid"},
        .name = "Invalid",
        .entries = {{.id = wiola::library::PlaylistEntryId{"entry-invalid"},
            .media = wiola::library::MediaId{"unknown"}}},
    };

    EXPECT_FALSE(repository.replace_all({track(retained)}, {invalid}, {}));
    ASSERT_EQ(repository.playlists().size(), 0);
    ASSERT_NE(repository.track(retained), nullptr);

    QFile corrupted{file()};
    ASSERT_TRUE(corrupted.open(QIODevice::WriteOnly));
    ASSERT_TRUE(corrupted.write("{not json") > 0);
    corrupted.close();

    wiola::gui::SavedPlaylistStorage storage{file()};
    EXPECT_EQ(storage.restore(repository), wiola::gui::LibraryJsonError::malformed);
    ASSERT_NE(repository.track(retained), nullptr);
    EXPECT_EQ(repository.tracks().size(), 1);
}

} // namespace

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    QCoreApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
