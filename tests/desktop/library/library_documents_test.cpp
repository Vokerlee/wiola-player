/**
 * @file
 * @brief Registration and state tests for shared player actions.
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

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <desktop/library/library_documents.hpp>
#include <gtest/gtest.h>
#include <qt/paths.hpp>

namespace {

using namespace wiola;

TEST(LibraryDocuments, FailedPersistenceRollsBackWorkspaceAndPlaylistImports)
{
    QTemporaryDir directory;
    library::InMemoryRepository repository;
    bool fail = true;
    app::LibraryDocuments documents{repository, [&](const auto&) { return !fail; }};
    const auto root = platform::qt::QtPathAdapter::from_qt(directory.path());
    EXPECT_EQ(documents.open_folder(root).error, app::LibraryDocumentError::write_failed);
    EXPECT_TRUE(repository.workspaces().empty());
    QFile file{directory.filePath("unicode-音楽.m3u8")};
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("#EXTM3U\ntrack.wav\n");
    file.close();
    const auto path = platform::qt::QtPathAdapter::from_qt(file.fileName());
    EXPECT_EQ(documents.import_m3u(path).error, app::LibraryDocumentError::write_failed);
    EXPECT_TRUE(repository.playlists().empty());
    EXPECT_TRUE(repository.tracks().empty());
    fail = false;
    const auto folder = documents.open_folder(root);
    ASSERT_EQ(folder.error, app::LibraryDocumentError::none);
    EXPECT_EQ(documents.set_recursive(folder.id, false), app::LibraryDocumentError::none);
    EXPECT_FALSE(repository.workspace(folder.id)->recursive);
    const auto imported = documents.import_m3u(path);
    ASSERT_EQ(imported.error, app::LibraryDocumentError::none);
    ASSERT_EQ(repository.playlist(imported.id)->entries.size(), 1);
    EXPECT_EQ(documents.export_m3u(imported.id, root), app::LibraryDocumentError::write_failed);
    EXPECT_EQ(repository.playlist(imported.id)->entries.size(), 1);
}

TEST(LibraryDocuments, OversizedSparseImportIsRejectedWithoutAllocatingOrCreatingRecords)
{
    QTemporaryDir directory;
    QFile file{directory.filePath("large.m3u8")};
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_TRUE(file.resize(16 * 1024 * 1024 + 1));
    file.close();
    library::InMemoryRepository repository;
    int saves = 0;
    app::LibraryDocuments documents{repository, [&](const auto&) {
                                        ++saves;
                                        return true;
                                    }};
    EXPECT_EQ(documents.import_m3u(platform::qt::QtPathAdapter::from_qt(file.fileName())).error,
        app::LibraryDocumentError::read_failed);
    EXPECT_EQ(saves, 0);
    EXPECT_TRUE(repository.tracks().empty());
    EXPECT_TRUE(repository.playlists().empty());
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app{argc, argv};
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
