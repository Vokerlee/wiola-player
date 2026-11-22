/**
 * @file
 * @brief Atomicity, coalescing, and recovery tests for library storage.
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

#include <persistence/qt/library_storage.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

namespace {

using namespace wiola::persistence::qt;
using wiola::library::TrackRecord;
using Storage = wiola::persistence::qt::LibraryStorage;

wiola::library::LibrarySnapshot snapshot(const std::string& title)
{
    wiola::library::LibrarySnapshot library;
    TrackRecord track;
    track.id = {.value = "media-1"};
    track.embedded.title = title;
    library.tracks.push_back(track);

    return library;
}

template<typename Predicate>
bool pump_until(Predicate&& predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};

    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }

    return predicate();
}

class LibraryStorage : public testing::Test {
protected:
    QTemporaryDir directory;

    std::filesystem::path file(const char* name = "library.json") const
    {
        return std::filesystem::path{directory.filePath(name).toStdString()};
    }
};

TEST_F(LibraryStorage, MissingFilesProduceAnEmptyLibrary)
{
    Storage storage{file()};
    wiola::library::LibrarySnapshot restored;

    EXPECT_EQ(storage.load(restored), LibraryJsonError::none);
    EXPECT_TRUE(restored.tracks.empty());
    EXPECT_TRUE(restored.playlists.empty());
}

TEST_F(LibraryStorage, WritesAndReadsACompleteSnapshot)
{
    Storage storage{file()};

    ASSERT_EQ(storage.save(snapshot("Stored")), LibraryJsonError::none);

    wiola::library::LibrarySnapshot restored;
    ASSERT_EQ(storage.load(restored), LibraryJsonError::none);
    ASSERT_EQ(restored.tracks.size(), 1U);
    EXPECT_EQ(restored.tracks.front().embedded.title, "Stored");
}

TEST_F(LibraryStorage, RecoversTheLastValidBackupAfterPrimaryCorruption)
{
    Storage storage{file()};

    ASSERT_EQ(storage.save(snapshot("First")), LibraryJsonError::none);
    ASSERT_EQ(storage.save(snapshot("Second")), LibraryJsonError::none);
    ASSERT_TRUE(std::filesystem::exists(file("library.json.bak")));

    QFile damaged{QString::fromStdString(file().string())};
    ASSERT_TRUE(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(damaged.write("broken"), 6);
    damaged.close();

    wiola::library::LibrarySnapshot restored;
    ASSERT_EQ(storage.load(restored), LibraryJsonError::none);
    ASSERT_EQ(restored.tracks.size(), 1U);
    EXPECT_EQ(restored.tracks.front().embedded.title, "First");
}

TEST_F(LibraryStorage, CoalescesScheduledSnapshotsIntoTheNewestCompleteWrite)
{
    Storage storage{file()};
    int failures{0};

    storage.on_save_failed([&failures](LibraryJsonError) { ++failures; });

    storage.schedule(snapshot("Old"));
    storage.schedule(snapshot("New"));

    ASSERT_TRUE(storage.pending());
    ASSERT_TRUE(pump_until([&storage] { return !storage.pending(); }));
    EXPECT_EQ(failures, 0);

    wiola::library::LibrarySnapshot restored;
    ASSERT_EQ(storage.load(restored), LibraryJsonError::none);
    ASSERT_EQ(restored.tracks.size(), 1U);
    EXPECT_EQ(restored.tracks.front().embedded.title, "New");
}

TEST_F(LibraryStorage, FlushWritesImmediatelyAndClearsThePendingSnapshot)
{
    Storage storage{file()};

    storage.schedule(snapshot("Immediate"));

    EXPECT_EQ(storage.flush(), LibraryJsonError::none);
    EXPECT_FALSE(storage.pending());

    wiola::library::LibrarySnapshot restored;
    ASSERT_EQ(storage.load(restored), LibraryJsonError::none);
    ASSERT_EQ(restored.tracks.size(), 1U);
    EXPECT_EQ(restored.tracks.front().embedded.title, "Immediate");
}

TEST_F(LibraryStorage, InvalidSnapshotDoesNotReplaceThePreviousFile)
{
    Storage storage{file()};
    ASSERT_EQ(storage.save(snapshot("Valid")), LibraryJsonError::none);

    wiola::library::LibrarySnapshot invalid{
        .tracks = {snapshot("One").tracks.front(), snapshot("Two").tracks.front()},
        .playlists = {}
    };

    EXPECT_EQ(storage.save(invalid), LibraryJsonError::invalid_record);

    wiola::library::LibrarySnapshot restored;
    ASSERT_EQ(storage.load(restored), LibraryJsonError::none);
    ASSERT_EQ(restored.tracks.size(), 1U);
    EXPECT_EQ(restored.tracks.front().embedded.title, "Valid");
}

TEST_F(LibraryStorage, ReportsAScheduledWriteFailureAndKeepsTheSnapshotForRetry)
{
    QFile blocker{QString::fromStdString(file("blocked").string())};
    ASSERT_TRUE(blocker.open(QIODevice::WriteOnly));
    blocker.close();

    Storage storage{file("blocked/library.json")};
    int failures{0};
    storage.on_save_failed([&failures](LibraryJsonError error) {
        EXPECT_EQ(error, LibraryJsonError::io);
        ++failures;
    });

    storage.schedule(snapshot("Retry me"));

    ASSERT_TRUE(pump_until([&failures] { return failures == 1; }));
    EXPECT_TRUE(storage.pending());
}

} // namespace

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    QCoreApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
