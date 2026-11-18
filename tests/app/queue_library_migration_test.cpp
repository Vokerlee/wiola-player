/**
 * @file
 * @brief Non-destructive queue migration tests.
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

#include <app/queue_library_migration.hpp>

#include <gui/library_storage.hpp>
#include <gui/player_settings.hpp>
#include <gui/qt_path_adapter.hpp>
#include <library/location.hpp>

#include <fixtures/wav.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace {

using Migration = wiola::app::QueueLibraryMigration;
using wiola::gui::LibraryJsonError;
using wiola::gui::LibrarySnapshot;
using wiola::gui::LibraryStorage;

class QueueLibraryMigration : public testing::Test {
protected:
    static void SetUpTestSuite()
    {
        settings_directory = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(settings_directory->isValid());

        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_directory->path());
    }

    static void TearDownTestSuite() { settings_directory.reset(); }

    void SetUp() override
    {
        settings.clear();
        settings.sync();
    }

    void TearDown() override
    {
        for (const std::filesystem::path& track : tracks)
            std::filesystem::remove(track);
    }

    std::filesystem::path track(const char* name)
    {
        tracks.push_back(wiola::testing::write_wav(name));

        return tracks.back();
    }

    std::filesystem::path library_file() const
    {
        const QString directory{QFileInfo{settings.fileName()}.absolutePath()};

        return wiola::gui::QtPathAdapter::from_qt(directory + "/library.json");
    }

    inline static std::unique_ptr<QTemporaryDir> settings_directory;
    QSettings settings{QSettings::IniFormat, QSettings::UserScope, "wiola-test", "migration"};
    std::vector<std::filesystem::path> tracks;
};

TEST_F(QueueLibraryMigration, CreatesAnEmptyLibraryForAnEmptyQueue)
{
    const Migration migration{settings};

    EXPECT_EQ(migration.migrate(settings), LibraryJsonError::none);
    EXPECT_TRUE(std::filesystem::exists(library_file()));

    LibraryStorage storage{library_file()};
    LibrarySnapshot restored;
    EXPECT_EQ(storage.load(restored), LibraryJsonError::none);
    EXPECT_TRUE(restored.tracks.empty());
    EXPECT_TRUE(restored.playlists.empty());
}

TEST_F(QueueLibraryMigration, PreservesDuplicateOccurrencesAndMissingTracks)
{
    const std::filesystem::path existing = track("wiola_migrate.wav");
    const std::filesystem::path missing{library_file().parent_path() / "missing.wav"};

    wiola::gui::PlaylistSettings queue;
    queue.tracks = {existing, existing, missing};
    queue.selected = existing;
    queue.repeat = 2;
    queue.shuffled = true;
    wiola::gui::QtSettingsAdapter{settings}.write_playlist(queue);

    const Migration migration{settings};
    ASSERT_EQ(migration.migrate(settings), LibraryJsonError::none);

    LibraryStorage storage{library_file()};
    LibrarySnapshot restored;
    ASSERT_EQ(storage.load(restored), LibraryJsonError::none);

    ASSERT_EQ(restored.tracks.size(), 2U);
    ASSERT_EQ(restored.playlists.size(), 1U);
    EXPECT_EQ(restored.playlists.front().name, "Migrated queue");
    ASSERT_EQ(restored.playlists.front().entries.size(), 3U);

    const auto& entries = restored.playlists.front().entries;
    EXPECT_EQ(entries[0].media, entries[1].media);
    EXPECT_NE(entries[0].id, entries[1].id);
    EXPECT_NE(entries[1].media, entries[2].media);

    for (const wiola::library::TrackRecord& track : restored.tracks) {
        if (track.id == wiola::library::media_id_for(existing))
            EXPECT_FALSE(track.missing);
        else
            EXPECT_TRUE(track.missing);
    }

    const auto unchanged = wiola::gui::QtSettingsAdapter{settings}.read_playlist();
    EXPECT_EQ(unchanged.tracks, queue.tracks);
    EXPECT_EQ(unchanged.selected, queue.selected);
    EXPECT_EQ(unchanged.repeat, queue.repeat);
    EXPECT_EQ(unchanged.shuffled, queue.shuffled);
}

TEST_F(QueueLibraryMigration, DoesNotOverwriteAnExistingValidLibrary)
{
    LibrarySnapshot existing;
    wiola::library::TrackRecord track;
    track.id = {.value = "existing-media"};
    track.embedded.title = "Keep me";
    existing.tracks.push_back(track);

    LibraryStorage storage{library_file()};
    ASSERT_EQ(storage.save(existing), LibraryJsonError::none);

    wiola::gui::PlaylistSettings queue;
    queue.tracks = {this->track("wiola_migrate_existing.wav")};
    wiola::gui::QtSettingsAdapter{settings}.write_playlist(queue);

    const Migration migration{settings};
    ASSERT_EQ(migration.migrate(settings), LibraryJsonError::none);

    LibrarySnapshot restored;
    ASSERT_EQ(storage.load(restored), LibraryJsonError::none);
    ASSERT_EQ(restored.tracks.size(), 1U);
    EXPECT_EQ(restored.tracks.front().embedded.title, "Keep me");
    EXPECT_TRUE(restored.playlists.empty());
}

TEST_F(QueueLibraryMigration, DoesNotReplaceACorruptLibrary)
{
    std::filesystem::create_directories(library_file().parent_path());

    QFile damaged{QString::fromStdString(library_file().string())};
    ASSERT_TRUE(damaged.open(QIODevice::WriteOnly));
    ASSERT_EQ(damaged.write("broken"), 6);
    damaged.close();

    const Migration migration{settings};

    EXPECT_EQ(migration.migrate(settings), LibraryJsonError::malformed);

    damaged.open(QIODevice::ReadOnly);
    EXPECT_EQ(damaged.readAll(), "broken");
}

} // namespace

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    QCoreApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
