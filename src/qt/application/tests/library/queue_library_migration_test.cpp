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

#include <app/library/local_identity.hpp>
#include <fixtures/wav.hpp>
#include <qt/application/library/queue_library_migration.hpp>
#include <qt/storage/library_store.hpp>
#include <wiola/qt/paths.hpp>
#include <wiola/qt/storage/settings.hpp>

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
using wiola::library::LibrarySnapshot;
using wiola::persistence::qt::LibraryJsonError;
using wiola::persistence::qt::LibraryStore;
using wiola::persistence::qt::LibraryStoreResult;

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

        return wiola::platform::qt::QtPathAdapter::from_qt(directory + "/library.json");
    }

    inline static std::unique_ptr<QTemporaryDir> settings_directory;
    QSettings settings{QSettings::IniFormat, QSettings::UserScope, "wiola-test", "migration"};
    std::vector<std::filesystem::path> tracks;
};

TEST_F(QueueLibraryMigration, CreatesAnEmptyLibraryForAnEmptyQueue)
{
    const Migration migration{settings};

    EXPECT_EQ(migration.migrate(settings), LibraryStoreResult{});
    EXPECT_TRUE(std::filesystem::exists(library_file()));

    LibraryStore storage{library_file()};
    wiola::library::LibrarySnapshot restored;

    EXPECT_EQ(storage.load(restored), LibraryStoreResult{});
    EXPECT_TRUE(restored.tracks.empty());
    EXPECT_TRUE(restored.playlists.empty());
}

TEST_F(QueueLibraryMigration, PreservesDuplicateOccurrencesAndMissingTracks)
{
    const std::filesystem::path existing = track("wiola_migrate.wav");
    const std::filesystem::path missing{library_file().parent_path() / "missing.wav"};

    wiola::app::QueueSnapshot queue;
    queue.tracks = {existing, existing, missing};
    queue.selected = existing;
    queue.repeat = wiola::engine::RepeatMode::all;
    queue.shuffled = true;
    wiola::platform::qt::QtSettingsAdapter{settings}.write_queue(queue);

    const Migration migration{settings};

    ASSERT_EQ(migration.migrate(settings), LibraryStoreResult{});

    LibraryStore storage{library_file()};
    wiola::library::LibrarySnapshot restored;

    ASSERT_EQ(storage.load(restored), LibraryStoreResult{});

    ASSERT_EQ(restored.tracks.size(), 2U);
    ASSERT_EQ(restored.playlists.size(), 1U);
    EXPECT_EQ(restored.playlists.front().name, "My playlist");
    ASSERT_EQ(restored.playlists.front().entries.size(), 3U);

    const auto& entries = restored.playlists.front().entries;

    EXPECT_EQ(entries[0].media, entries[1].media);
    EXPECT_NE(entries[0].id, entries[1].id);
    EXPECT_NE(entries[1].media, entries[2].media);

    for (const wiola::library::TrackRecord& track : restored.tracks) {
        if (track.id == wiola::app::media_id_for(existing))
            EXPECT_FALSE(track.missing);
        else
            EXPECT_TRUE(track.missing);
    }

    const auto unchanged = wiola::platform::qt::QtSettingsAdapter{settings}.read_queue();

    EXPECT_EQ(unchanged.tracks, queue.tracks);
    EXPECT_EQ(unchanged.selected, queue.selected);
    EXPECT_EQ(unchanged.repeat, queue.repeat);
    EXPECT_EQ(unchanged.shuffled, queue.shuffled);
}

TEST_F(QueueLibraryMigration, DoesNotOverwriteAnExistingValidLibrary)
{
    wiola::library::LibrarySnapshot existing;
    wiola::library::TrackRecord track;
    track.id = {.value = "existing-media"};
    track.embedded.title = "Keep me";
    existing.tracks.push_back(track);

    LibraryStore storage{library_file()};

    ASSERT_EQ(storage.save(existing), LibraryStoreResult{});

    wiola::app::QueueSnapshot queue;
    queue.tracks = {this->track("wiola_migrate_existing.wav")};
    wiola::platform::qt::QtSettingsAdapter{settings}.write_queue(queue);

    const Migration migration{settings};

    ASSERT_EQ(migration.migrate(settings), LibraryStoreResult{});

    wiola::library::LibrarySnapshot restored;

    ASSERT_EQ(storage.load(restored), LibraryStoreResult{});
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

    EXPECT_EQ(migration.migrate(settings),
        LibraryStoreResult{.schema = LibraryJsonError::malformed});

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
