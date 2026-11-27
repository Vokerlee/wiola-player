/**
 * @file
 * @brief Workspace scan admission, reconciliation and relocation contracts.
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

#include <wiola/library/in_memory_repository.hpp>
#include <wiola/presentation/documents/workspace_rows.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace {

using namespace wiola;

class WorkspaceRowsTest : public testing::Test {
protected:
    library::WorkspaceRecord workspace{library::WorkspaceId{"folder"}, "music", true};
    gui::WorkspaceRows rows;

    app::WorkspaceScanBatch batch(std::uint64_t generation,
        std::vector<app::WorkspaceScanEntry> entries = {},
        app::WorkspaceScanStatus status = app::WorkspaceScanStatus::running) const
    {
        app::WorkspaceScanBatch result;
        result.workspace = workspace.id;
        result.generation = generation;
        result.entries = std::move(entries);
        result.status = status;
        return result;
    }
};

TEST_F(WorkspaceRowsTest, RejectsInvalidStaleAndForeignBatchesWithoutChangingRows)
{
    EXPECT_FALSE(rows.begin({}, 1));
    EXPECT_FALSE(rows.begin(workspace, 0));
    EXPECT_FALSE(rows.apply(batch(1), nullptr));
    ASSERT_TRUE(rows.begin(workspace, 2));
    EXPECT_FALSE(rows.begin(workspace, 2));
    EXPECT_FALSE(rows.begin(workspace, 1));
    EXPECT_FALSE(rows.apply(batch(1), nullptr));
    auto foreign = batch(2);
    foreign.workspace = library::WorkspaceId{"other"};
    EXPECT_FALSE(rows.apply(foreign, nullptr));
    EXPECT_TRUE(rows.rows().empty());

    rows.cancel();
    EXPECT_FALSE(rows.apply(batch(2), nullptr));
}

TEST_F(WorkspaceRowsTest, OrdersByLocationDeduplicatesMediaAndPrunesOnlyOnCompletion)
{
    const library::MediaId a{"a"}, b{"b"};
    ASSERT_TRUE(rows.begin(workspace, 1));
    ASSERT_TRUE(rows.apply(
        batch(1,
            {
                {b,  "music/b.flac",       std::nullopt},
                {a,  "music/a.flac",       std::nullopt},
                {a,  "music/ignored.flac", std::nullopt},
                {{}, "music/invalid.flac", std::nullopt}
    }),
        nullptr
    ));
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows.rows()[0].media, a);
    EXPECT_EQ(rows.rows()[1].media, b);
    EXPECT_EQ(rows.rows()[0].metadata.title, "a");
    const auto snapshot = rows.rows();
    const auto revision = rows.revision();

    ASSERT_TRUE(rows.begin(workspace, 2));
    ASSERT_TRUE(rows.apply(batch(2,
                               {
                                   {a, "music/a.flac", std::nullopt}
    }),
        nullptr));
    EXPECT_EQ(rows.rows(), snapshot);
    EXPECT_EQ(rows.revision(), revision);
    ASSERT_TRUE(rows.apply(batch(2, {}, app::WorkspaceScanStatus::complete), nullptr));
    ASSERT_EQ(rows.size(), 1);
    EXPECT_GT(rows.revision(), revision);
    EXPECT_EQ(rows.row(gui::WorkspaceTrackId{workspace.id, b}), nullptr);
    EXPECT_FALSE(rows.apply(batch(2), nullptr));
    EXPECT_EQ(snapshot.size(), 2);
}

TEST_F(WorkspaceRowsTest, FailedAndCancelledScansRetainUnseenRows)
{
    ASSERT_TRUE(rows.begin(workspace, 1));
    ASSERT_TRUE(rows.apply(batch(1,
                               {
                                   {library::MediaId{"media"}, "music/a.flac", std::nullopt}
    },
                               app::WorkspaceScanStatus::complete),
        nullptr));
    const auto snapshot = rows.rows();

    for (const auto status :
        {app::WorkspaceScanStatus::failed, app::WorkspaceScanStatus::cancelled}) {
        const auto generation = status == app::WorkspaceScanStatus::failed ? 2u : 3u;
        ASSERT_TRUE(rows.begin(workspace, generation));
        ASSERT_TRUE(rows.apply(batch(generation, {}, status), nullptr));
        EXPECT_EQ(rows.rows(), snapshot);
        EXPECT_FALSE(rows.apply(batch(generation), nullptr));
    }

    auto other = workspace;
    other.id = library::WorkspaceId{"other"};
    EXPECT_TRUE(rows.begin(other, 1));
    EXPECT_TRUE(rows.rows().empty());
}

TEST_F(WorkspaceRowsTest, UsesCatalogMetadataButPreservesScannedLocationAndAvailability)
{
    library::InMemoryRepository repository;
    library::TrackRecord track;
    track.id = library::MediaId{"media"};
    track.embedded.title = "Catalog title";
    track.locations = {"music/preferred.flac", "music/scanned.flac"};
    ASSERT_TRUE(repository.put_track(track));
    ASSERT_TRUE(rows.begin(workspace, 1));
    app::WorkspaceScanEntry entry{track.id, "music/scanned.flac", std::nullopt};
    entry.unreadable = true;
    ASSERT_TRUE(rows.apply(batch(1, {entry}), &repository));
    const gui::TrackRowId id = gui::WorkspaceTrackId{workspace.id, track.id};
    ASSERT_NE(rows.row(id), nullptr);
    EXPECT_EQ(rows.row(id)->metadata.title, "Catalog title");
    EXPECT_EQ(rows.row(id)->location, entry.location);
    EXPECT_TRUE(rows.row(id)->missing);
    EXPECT_EQ(rows.row(engine::QueueEntryId{1}), nullptr);
    EXPECT_EQ(rows.row(gui::WorkspaceTrackId{library::WorkspaceId{"other"}, track.id}), nullptr);
    EXPECT_EQ(rows.row(gui::WorkspaceTrackId{workspace.id, library::MediaId{"absent"}}), nullptr);

    track.overrides.title = "Changed";
    ASSERT_TRUE(repository.put_track(track));
    EXPECT_TRUE(rows.update_media(repository, track.id));
    EXPECT_EQ(rows.row(id)->location, entry.location);
    EXPECT_EQ(rows.row(id)->metadata.title, "Changed");
    EXPECT_FALSE(rows.update_media(repository, track.id));
    EXPECT_FALSE(rows.update_media(repository, library::MediaId{"absent"}));

    track.locations = {"music/relocated.flac"};
    ASSERT_TRUE(repository.put_track(track));
    EXPECT_TRUE(rows.update_media(repository, track.id));
    EXPECT_EQ(rows.rows().front().location, track.locations.front());
    EXPECT_EQ(rows.size(), 1);

    track.locations.clear();
    ASSERT_TRUE(repository.put_track(track));
    EXPECT_TRUE(rows.update_media(repository, track.id));
    EXPECT_EQ(rows.row(id)->location, "music/relocated.flac");
}

TEST_F(WorkspaceRowsTest, PreservesExplicitlyEmptyTitleAndHandlesUncataloguedMedia)
{
    library::InMemoryRepository repository;
    library::TrackRecord track;
    track.id = library::MediaId{"media"};
    track.overrides.title = "";
    ASSERT_TRUE(repository.put_track(track));
    ASSERT_TRUE(rows.begin(workspace, 1));
    ASSERT_TRUE(
        rows.apply(batch(1,
                       {
                           {track.id,                    "music/filename.flac", std::nullopt},
                           {library::MediaId{"unknown"}, "music/other.flac",    std::nullopt}
    }),
            &repository)
    );

    EXPECT_TRUE(rows.row(gui::WorkspaceTrackId{workspace.id, track.id})->metadata.title.empty());
    EXPECT_EQ(
        rows.row(gui::WorkspaceTrackId{workspace.id, library::MediaId{"unknown"}})->metadata.title,
        "other"
    );
    EXPECT_FALSE(rows.update_media(repository, library::MediaId{"unknown"}));
}

} // namespace
