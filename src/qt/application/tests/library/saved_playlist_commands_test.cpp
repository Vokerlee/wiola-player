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

#include <qt/storage/library_store.hpp>
#include <wiola/app/library/playlist_name.hpp>
#include <wiola/app/library/saved_playlist_commands.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <limits>

namespace {

void expect_compatible_name(const std::string& text)
{
    const auto expected = QString::fromStdString(text).trimmed();
    const bool valid = !expected.isEmpty() &&
        expected.size() <= static_cast<qsizetype>(wiola::app::PlaylistName::max_utf16_units);
    const auto actual = wiola::app::PlaylistName::parse(text);

    ASSERT_EQ(actual.has_value(), valid) << "input=" << text;

    if (actual)
        EXPECT_EQ(actual->value(), expected.toStdString()) << "input=" << text;
}

TEST(PlaylistNameCompatibility, MatchesQtForEveryTwoByteInput)
{
    constexpr auto maximum = std::numeric_limits<unsigned char>::max();
    for (unsigned first = 0; first <= maximum; ++first)
        for (unsigned second = 0; second <= maximum; ++second)
            expect_compatible_name(std::string{static_cast<char>(first),
                static_cast<char>(second)});
}

TEST(PlaylistNameCompatibility, MatchesQtForScalarBoundsMalformedSequencesAndTrimmedLength)
{
    for (const std::string& text : {std::string{"\xef\xbb\xbf"
                                                "  Music  "},
             std::string{"\xef\xbb\xbf\xef\xbb\xbf"}, std::string{"\xe0\xa0\x80"},
             std::string{"\xed\x9f\xbf"}, std::string{"\xed\xa0\x80"}, std::string{"\xee\x80\x80"},
             std::string{"\xf0\x90\x80\x80"}, std::string{"\xf4\x8f\xbf\xbf"},
             std::string{"\xf4\x90\x80\x80"}, std::string{"\xe2\xff\x80"},
             std::string{"\xf0\x9f\x92"},
             std::string{"\xf0\x90\x80"
                         "A"},
             std::string{"\xc2\xa0\xe3\x80\x80"
                         "Music"
                         "\xe2\x80\xaf"},
             std::string(wiola::app::PlaylistName::max_utf16_units, 'x') + "  ",
             std::string(wiola::app::PlaylistName::max_utf16_units + 1, 'x')})
        expect_compatible_name(text);
}

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
    wiola::persistence::qt::LibraryStore storage{file()};
    auto persist = [&storage](const wiola::library::LibraryRepository& records) {
        return storage.save(wiola::library::snapshot_of(records)) ==
            wiola::persistence::qt::LibraryStoreResult{};
    };
    wiola::app::SavedPlaylistCommands commands{repository, persist};
    const wiola::library::MediaId media{"media-1"};
    wiola::library::TrackRecord track;
    track.id = media;
    ASSERT_TRUE(repository.put_track(track));

    const auto original = commands.create("  Road trip  ").id;

    ASSERT_TRUE(original.valid());
    ASSERT_EQ(repository.playlist(original)->name, "Road trip");
    ASSERT_TRUE(repository.append_track(original, media));
    ASSERT_TRUE(repository.append_track(original, media));
    ASSERT_EQ(storage.save(wiola::library::snapshot_of(repository)),
        wiola::persistence::qt::LibraryStoreResult{});

    ASSERT_TRUE(commands.rename(original, "Evening"));

    const auto copy = commands.duplicate(original).id;

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

    ASSERT_EQ(wiola::persistence::qt::restore_repository(storage, restored),
        wiola::persistence::qt::LibraryStoreResult{});
    EXPECT_EQ(restored.playlist(original), nullptr);
    ASSERT_NE(restored.playlist(copy), nullptr);
    EXPECT_EQ(restored.playlist(copy)->entries.size(), 2);
}

TEST_F(SavedPlaylistCommands, RejectsBlankNamesAndMissingPlaylists)
{
    wiola::persistence::qt::LibraryStore storage{file()};
    auto persist = [&storage](const wiola::library::LibraryRepository& records) {
        return storage.save(wiola::library::snapshot_of(records)) ==
            wiola::persistence::qt::LibraryStoreResult{};
    };
    wiola::app::SavedPlaylistCommands commands{repository, persist};

    EXPECT_FALSE(commands.create("   ").id.valid());
    EXPECT_FALSE(
        commands.create(std::string(wiola::app::PlaylistName::max_utf16_units + 1, 'x')).id.valid()
    );
    EXPECT_TRUE(repository.playlists().empty());
    EXPECT_FALSE(commands.rename(wiola::library::PlaylistId{"missing"}, "Name"));
    EXPECT_FALSE(commands.duplicate(wiola::library::PlaylistId{"missing"}).id.valid());

    const auto id = commands.create("Valid").id;

    ASSERT_TRUE(id.valid());
    EXPECT_FALSE(commands.rename(id, "  "));
    EXPECT_EQ(repository.playlist(id)->name, "Valid");
}

TEST_F(SavedPlaylistCommands, RollsBackRepositoryChangesWhenPersistenceFails)
{
    ASSERT_TRUE(QDir{directory.path()}.mkdir("blocked"));

    wiola::persistence::qt::LibraryStore storage{file("blocked")};
    auto persist = [&storage](const wiola::library::LibraryRepository& records) {
        return storage.save(wiola::library::snapshot_of(records)) ==
            wiola::persistence::qt::LibraryStoreResult{};
    };
    wiola::app::SavedPlaylistCommands commands{repository, persist};

    EXPECT_FALSE(commands.create("Cannot save").id.valid());
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
