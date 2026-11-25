/**
 * @file
 * @brief Snapshot writing port adaptation and retained Qt storage diagnostics.
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

#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <qt/paths.hpp>
#include <qt/storage/library_store.hpp>
#include <qt/storage/snapshot_writer.hpp>

namespace {

using namespace wiola;

TEST(QtSnapshotWriter, TranslatesFormatAndFileFailuresAtTheDriverBoundary)
{
    using Error = app::SnapshotWriteError;
    using Schema = persistence::qt::LibraryJsonError;
    using Io = persistence::qt::FileStoreError;
    const std::vector<std::pair<persistence::qt::LibraryStoreResult, Error>> cases{
        {{},                                      Error::none               },
        {{.io = Io::io},                          Error::io                 },
        {{.io = Io::too_large},                   Error::too_large          },
        {{.schema = Schema::unsupported_version}, Error::unsupported_version},
        {{.schema = Schema::too_large},           Error::too_large          },
        {{.schema = Schema::limit_exceeded},      Error::too_large          },
        {{.schema = Schema::malformed},           Error::invalid_snapshot   },
        {{.schema = Schema::invalid_record},      Error::invalid_snapshot   },
    };
    for (const auto& [detail, expected] : cases) {
        const auto result = persistence::qt::snapshot_write_result(detail);
        EXPECT_EQ(result.error, expected);
        EXPECT_EQ(result.succeeded(), expected == Error::none);
    }
}

TEST(QtSnapshotWriter, WritesThroughThePortablePortAndPreservesTheStoredSnapshot)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = platform::qt::QtPathAdapter::from_qt(directory.filePath("library.json"));
    persistence::qt::LibraryStore store{path};
    persistence::qt::SnapshotWriter adapter{store};
    app::SnapshotWriter& writer = adapter;
    library::LibrarySnapshot snapshot;
    library::TrackRecord track;
    track.id = {"media"};
    track.embedded.title = "Owned";
    snapshot.tracks.push_back(track);
    ASSERT_TRUE(writer.write(snapshot).succeeded());
    library::LibrarySnapshot restored;
    ASSERT_TRUE(store.load(restored).succeeded());
    EXPECT_EQ(restored.tracks, snapshot.tracks);
    snapshot.tracks.front().id = {};
    EXPECT_EQ(writer.write(snapshot).error, app::SnapshotWriteError::invalid_snapshot);
    ASSERT_TRUE(store.load(restored).succeeded());
    EXPECT_EQ(restored.tracks.front(), track);
}

} // namespace
