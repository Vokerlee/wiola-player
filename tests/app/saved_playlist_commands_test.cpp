/**
 * @file
 * @brief Transactional saved playlist command regression tests.
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

#include <app/saved_playlist_commands.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace {

class SavedPlaylistCommands : public testing::Test {
protected:
    std::filesystem::path file(const char* name = "library.json") const
    {
        return std::filesystem::path{directory.filePath(name).toStdString()};
    }

    QTemporaryDir directory;
    wiola::library::InMemoryRepository repository;
};

TEST_F(SavedPlaylistCommands, CreatesRenamesDuplicatesAndDeletesPersistentPlaylists)
{
    wiola::gui::SavedPlaylistStorage storage{file()};
    wiola::app::SavedPlaylistCommands commands{repository, storage};
    const wiola::library::MediaId media{"media-1"};
    wiola::library::TrackRecord track;
    track.id = media;
    ASSERT_TRUE(repository.put_track(track));

    const auto original = commands.create("  Road trip  ");
    ASSERT_TRUE(original.valid());
    ASSERT_EQ(repository.playlist(original)->name, "Road trip");
    ASSERT_TRUE(repository.append_track(original, media));
    ASSERT_TRUE(repository.append_track(original, media));
    ASSERT_EQ(storage.save(repository), wiola::gui::LibraryJsonError::none);

    ASSERT_TRUE(commands.rename(original, "Evening"));
    const auto copy = commands.duplicate(original);
    ASSERT_TRUE(copy.valid());
    EXPECT_NE(copy, original);

    const wiola::library::PlaylistRecord* duplicated = repository.playlist(copy);
    ASSERT_NE(duplicated, nullptr);
    ASSERT_EQ(duplicated->entries.size(), 2);
    EXPECT_EQ(duplicated->name, "Evening copy");
    EXPECT_EQ(duplicated->entries[0].media, media);
    EXPECT_EQ(duplicated->entries[1].media, media);
    EXPECT_NE(duplicated->entries[0].id, duplicated->entries[1].id);

    const auto original_first = repository.playlist(original)->entries.front().id;
    EXPECT_NE(duplicated->entries.front().id, original_first);
    EXPECT_TRUE(commands.remove(original));
    EXPECT_EQ(repository.playlist(original), nullptr);
    EXPECT_FALSE(commands.remove(original));

    wiola::library::InMemoryRepository restored;
    ASSERT_EQ(storage.restore(restored), wiola::gui::LibraryJsonError::none);
    EXPECT_EQ(restored.playlist(original), nullptr);
    ASSERT_NE(restored.playlist(copy), nullptr);
    EXPECT_EQ(restored.playlist(copy)->entries.size(), 2);
}

TEST_F(SavedPlaylistCommands, RejectsBlankNamesAndMissingPlaylists)
{
    wiola::gui::SavedPlaylistStorage storage{file()};
    wiola::app::SavedPlaylistCommands commands{repository, storage};

    EXPECT_FALSE(commands.create("   ").valid());
    EXPECT_FALSE(commands.create(std::string(257, 'x')).valid());
    EXPECT_TRUE(repository.playlists().empty());
    EXPECT_FALSE(commands.rename(wiola::library::PlaylistId{"missing"}, "Name"));
    EXPECT_FALSE(commands.duplicate(wiola::library::PlaylistId{"missing"}).valid());

    const auto id = commands.create("Valid");
    ASSERT_TRUE(id.valid());
    EXPECT_FALSE(commands.rename(id, "  "));
    EXPECT_EQ(repository.playlist(id)->name, "Valid");
}

TEST_F(SavedPlaylistCommands, RollsBackRepositoryChangesWhenPersistenceFails)
{
    ASSERT_TRUE(QDir{directory.path()}.mkdir("blocked"));

    wiola::gui::SavedPlaylistStorage storage{file("blocked")};
    wiola::app::SavedPlaylistCommands commands{repository, storage};

    EXPECT_FALSE(commands.create("Cannot save").valid());
    EXPECT_TRUE(repository.playlists().empty());

    const auto retained = repository.create_playlist("Retained");
    EXPECT_FALSE(commands.remove(retained));
    ASSERT_NE(repository.playlist(retained), nullptr);
    EXPECT_EQ(repository.playlist(retained)->name, "Retained");

    EXPECT_FALSE(commands.rename(retained, "Cannot save either"));
    ASSERT_NE(repository.playlist(retained), nullptr);
    EXPECT_EQ(repository.playlist(retained)->name, "Retained");
}

} // namespace

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    QCoreApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
