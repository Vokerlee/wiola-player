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

#include <persistence/qt/library_store.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

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

TEST_F(LibraryStorage, NativeOutputOpenFailurePreservesTheExistingDirectoryAndContents)
{
    const auto target = file("existing-directory");
    ASSERT_TRUE(std::filesystem::create_directory(target));
    const auto sentinel = target / "keep.txt";
    {
        QFile file{QString::fromStdString(sentinel.string())};
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write("retained"), 8);
    }
    AtomicFileStore store{target, 100};
    EXPECT_EQ(store.write(FileSlot::primary, QByteArrayView{"replacement"}), FileStoreError::io);
    EXPECT_TRUE(std::filesystem::is_directory(target));
    QFile retained{QString::fromStdString(sentinel.string())};
    ASSERT_TRUE(retained.open(QIODevice::ReadOnly));
    EXPECT_EQ(retained.readAll(), "retained");
}

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
