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

#include <persistence/qt/library_save_queue.hpp>
#include <persistence/qt/library_store.hpp>

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
using Storage = wiola::persistence::qt::LibraryStore;

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

    EXPECT_EQ(storage.load(restored), LibraryStoreResult{});
    EXPECT_TRUE(restored.tracks.empty());
    EXPECT_TRUE(restored.playlists.empty());
}

TEST_F(LibraryStorage, WritesAndReadsACompleteSnapshot)
{
    Storage storage{file()};

    ASSERT_EQ(storage.save(snapshot("Stored")), LibraryStoreResult{});

    wiola::library::LibrarySnapshot restored;
    ASSERT_EQ(storage.load(restored), LibraryStoreResult{});
    ASSERT_EQ(restored.tracks.size(), 1U);
    EXPECT_EQ(restored.tracks.front().embedded.title, "Stored");
}

TEST_F(LibraryStorage, RecoversTheLastValidBackupAfterPrimaryCorruption)
{
    Storage storage{file()};

    ASSERT_EQ(storage.save(snapshot("First")), LibraryStoreResult{});
    ASSERT_EQ(storage.save(snapshot("Second")), LibraryStoreResult{});
    ASSERT_TRUE(std::filesystem::exists(file("library.json.bak")));

    QFile damaged{QString::fromStdString(file().string())};
    ASSERT_TRUE(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(damaged.write("broken"), 6);
    damaged.close();

    wiola::library::LibrarySnapshot restored;
    ASSERT_EQ(storage.load(restored), LibraryStoreResult{});
    ASSERT_EQ(restored.tracks.size(), 1U);
    EXPECT_EQ(restored.tracks.front().embedded.title, "First");
}

TEST_F(LibraryStorage, CoalescesScheduledSnapshotsIntoTheNewestCompleteWrite)
{
    Storage file_store{file()};
    LibrarySaveQueue storage{file_store};
    int failures{0};

    storage.on_save_failed([&failures](LibraryStoreResult) { ++failures; });

    storage.schedule(snapshot("Old"));
    storage.schedule(snapshot("New"));

    ASSERT_TRUE(storage.pending());
    ASSERT_TRUE(pump_until([&storage] { return !storage.pending(); }));
    EXPECT_EQ(failures, 0);

    wiola::library::LibrarySnapshot restored;
    ASSERT_EQ(file_store.load(restored), LibraryStoreResult{});
    ASSERT_EQ(restored.tracks.size(), 1U);
    EXPECT_EQ(restored.tracks.front().embedded.title, "New");
}

TEST_F(LibraryStorage, FlushWritesImmediatelyAndClearsThePendingSnapshot)
{
    Storage file_store{file()};
    LibrarySaveQueue storage{file_store};

    storage.schedule(snapshot("Immediate"));

    EXPECT_EQ(storage.flush(), LibraryStoreResult{});
    EXPECT_FALSE(storage.pending());

    wiola::library::LibrarySnapshot restored;
    ASSERT_EQ(file_store.load(restored), LibraryStoreResult{});
    ASSERT_EQ(restored.tracks.size(), 1U);
    EXPECT_EQ(restored.tracks.front().embedded.title, "Immediate");
}

TEST_F(LibraryStorage, InvalidSnapshotDoesNotReplaceThePreviousFile)
{
    Storage storage{file()};
    ASSERT_EQ(storage.save(snapshot("Valid")), LibraryStoreResult{});

    wiola::library::LibrarySnapshot invalid{
        .tracks = {snapshot("One").tracks.front(), snapshot("Two").tracks.front()},
        .playlists = {}
    };

    EXPECT_EQ(storage.save(invalid),
        LibraryStoreResult{.schema = LibraryJsonError::invalid_record});

    wiola::library::LibrarySnapshot restored;
    ASSERT_EQ(storage.load(restored), LibraryStoreResult{});
    ASSERT_EQ(restored.tracks.size(), 1U);
    EXPECT_EQ(restored.tracks.front().embedded.title, "Valid");
}

TEST_F(LibraryStorage, ReportsAScheduledWriteFailureAndKeepsTheSnapshotForRetry)
{
    QFile blocker{QString::fromStdString(file("blocked").string())};
    ASSERT_TRUE(blocker.open(QIODevice::WriteOnly));
    blocker.close();

    Storage file_store{file("blocked/library.json")};
    LibrarySaveQueue storage{file_store};
    int failures{0};
    storage.on_save_failed([&failures](LibraryStoreResult error) {
        EXPECT_EQ(error, LibraryStoreResult{.io = FileStoreError::io});
        ++failures;
    });

    storage.schedule(snapshot("Retry me"));

    ASSERT_TRUE(pump_until([&failures] { return failures == 1; }));
    EXPECT_TRUE(storage.pending());
}

TEST_F(LibraryStorage, FailedImmediateSaveRetainsTheNewestSnapshotForRetry)
{
    QFile blocker{directory.filePath("blocked")};
    ASSERT_TRUE(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    Storage store{file("blocked/library.json")};
    LibrarySaveQueue queue{store};
    queue.schedule(snapshot("Old"));
    EXPECT_EQ(queue.save(snapshot("Newest")), (LibraryStoreResult{.io = FileStoreError::io}));
    EXPECT_TRUE(queue.pending());
    ASSERT_TRUE(blocker.remove());
    ASSERT_TRUE(queue.flush().succeeded());
    EXPECT_FALSE(queue.pending());
    wiola::library::LibrarySnapshot restored;
    ASSERT_TRUE(store.load(restored).succeeded());
    ASSERT_EQ(restored.tracks.size(), 1U);
    EXPECT_EQ(restored.tracks.front().embedded.title, "Newest");
}

TEST_F(LibraryStorage, InvalidImmediateSaveKeepsAnEarlierValidPendingSnapshot)
{
    Storage store{file()};
    LibrarySaveQueue queue{store};
    queue.schedule(snapshot("Pending"));
    auto invalid = snapshot("Invalid");
    invalid.tracks.push_back(invalid.tracks.front());
    EXPECT_EQ(queue.save(invalid),
        (LibraryStoreResult{.schema = LibraryJsonError::invalid_record}));
    ASSERT_TRUE(queue.flush().succeeded());
    wiola::library::LibrarySnapshot restored;
    ASSERT_TRUE(store.load(restored).succeeded());
    EXPECT_EQ(restored.tracks.front().embedded.title, "Pending");
}

TEST_F(LibraryStorage, FutureSchemaIsNotReplacedByAnOlderBackupOrNewSave)
{
    Storage store{file()};
    ASSERT_TRUE(store.save(snapshot("Backup")).succeeded());
    ASSERT_TRUE(store.save(snapshot("Previous")).succeeded());
    const QByteArray future{"{\"version\":99}"};
    QFile output{directory.filePath("library.json")};
    ASSERT_TRUE(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(output.write(future), future.size());
    output.close();
    auto restored = snapshot("Retained");
    const LibraryStoreResult unsupported{.schema = LibraryJsonError::unsupported_version};
    EXPECT_EQ(store.load(restored), unsupported);
    EXPECT_EQ(restored.tracks.front().embedded.title, "Retained");
    EXPECT_EQ(store.save(snapshot("Must not overwrite")), unsupported);
    ASSERT_TRUE(output.open(QIODevice::ReadOnly));
    EXPECT_EQ(output.readAll(), future);
}

TEST_F(LibraryStorage, FutureBackupIsPreservedWhenThePrimaryIsDamaged)
{
    QFile primary{directory.filePath("library.json")};
    ASSERT_TRUE(primary.open(QIODevice::WriteOnly));
    ASSERT_GT(primary.write("broken"), 0);
    primary.close();
    QFile backup{directory.filePath("library.json.bak")};
    ASSERT_TRUE(backup.open(QIODevice::WriteOnly));
    ASSERT_GT(backup.write("{\"version\":99}"), 0);
    backup.close();
    Storage store{file()};
    wiola::library::LibrarySnapshot restored;
    const LibraryStoreResult unsupported{.schema = LibraryJsonError::unsupported_version};
    EXPECT_EQ(store.load(restored), unsupported);
    EXPECT_EQ(store.save(snapshot("Must not overwrite")), unsupported);
}

TEST_F(LibraryStorage, RejectsOversizedFilesBeforeReadingTheirContents)
{
    QFile oversized{directory.filePath("library.json")};
    ASSERT_TRUE(oversized.open(QIODevice::WriteOnly));
    ASSERT_TRUE(oversized.resize(max_library_document_bytes + 1));
    oversized.close();
    AtomicFileStore bytes{file(), max_library_document_bytes};
    const auto read = bytes.read(FileSlot::primary);
    EXPECT_EQ(read.error, FileStoreError::too_large);
    EXPECT_TRUE(read.bytes.isEmpty());
    Storage store{file()};
    auto restored = snapshot("Retained");
    EXPECT_EQ(store.load(restored), (LibraryStoreResult{.io = FileStoreError::too_large}));
    EXPECT_EQ(restored.tracks.front().embedded.title, "Retained");
}

class FailingOutput final : public AtomicOutput {
public:
    FailingOutput(bool short_write, int& commits)
        : short_write_{short_write}
        , commits_{commits}
    {
    }

    qint64 write(QByteArrayView bytes) override
    {
        return short_write_ ? bytes.size() - 1 : bytes.size();
    }

    bool commit() override
    {
        ++commits_;
        return false;
    }

private:
    bool short_write_;
    int& commits_;
};

TEST_F(LibraryStorage, ShortWritesAreNeverCommitted)
{
    int commits = 0;
    AtomicFileStore bytes{file(), 100,
        [&commits](const auto&) { return std::make_unique<FailingOutput>(true, commits); }};
    EXPECT_EQ(bytes.write(FileSlot::primary, QByteArrayView{"complete"}), FileStoreError::io);
    EXPECT_EQ(commits, 0);
    EXPECT_FALSE(std::filesystem::exists(file()));
}

TEST_F(LibraryStorage, BackupCommitFailureLeavesThePrimaryUnchanged)
{
    Storage original{file()};
    ASSERT_TRUE(original.save(snapshot("Original")).succeeded());
    int commits = 0;
    int outputs = 0;
    Storage failing{file(), [&commits, &outputs](const std::filesystem::path& target) {
                        EXPECT_EQ(target.extension(), ".bak");
                        ++outputs;
                        return std::make_unique<FailingOutput>(false, commits);
                    }};
    EXPECT_EQ(failing.save(snapshot("Rejected")), (LibraryStoreResult{.io = FileStoreError::io}));
    EXPECT_EQ(outputs, 1);
    EXPECT_EQ(commits, 1);
    wiola::library::LibrarySnapshot restored;
    ASSERT_TRUE(original.load(restored).succeeded());
    EXPECT_EQ(restored.tracks.front().embedded.title, "Original");
}

} // namespace

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    QCoreApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
