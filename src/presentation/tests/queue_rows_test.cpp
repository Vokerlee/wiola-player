/**
 * @file
 * @brief Portable playback queue reconciliation and catalog update contracts.
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
#include <queue/playlist.hpp>
#include <wiola/library/in_memory_repository.hpp>
#include <wiola/media/local/resource.hpp>
#include <wiola/presentation/documents/queue_rows.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace {

using namespace wiola;

TEST(QueueRows, ConvertsOnlyQueueOccurrencesAndKeepsRemoteResourcesOpaque)
{
    engine::Playlist playlist;
    playlist.set({
        media::local_resource("song.flac"), {"remote", "opaque-key", "Remote song"}
    });
    const auto rows = gui::queue_rows(playlist.tracks());
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0].location, "song.flac");
    EXPECT_TRUE(rows[1].location.empty());
    EXPECT_TRUE(rows[1].location_key.empty());
    EXPECT_FALSE(rows[1].media.valid());

    const auto first = playlist.tracks()[0].id();
    const auto second = playlist.tracks()[1].id();
    const std::vector<gui::TrackRowId> selection{
        first, library::PlaylistEntryId{"saved"},
        gui::WorkspaceTrackId{library::WorkspaceId{"folder"}, library::MediaId{"media"}},
         second
    };
    EXPECT_EQ(gui::queue_entry_ids(selection), (std::vector{first, second}));
    EXPECT_EQ(gui::queue_entry_id(selection[0]), first);
    EXPECT_FALSE(gui::queue_entry_id(selection[1]));
    EXPECT_FALSE(gui::queue_entry_id(std::nullopt));
    EXPECT_TRUE(gui::queue_entry_ids({}).empty());
}

TEST(QueueRows, ReconcilesStructureSelectionAndOnlyChangedMetadataOccurrences)
{
    engine::Playlist playlist;
    playlist.set({media::local_resource("same.flac"), media::local_resource("same.flac")});
    gui::QueueRows rows;
    const auto first = playlist.tracks()[0].id();
    const auto second = playlist.tracks()[1].id();
    const auto initial = rows.reconcile(playlist, nullptr);
    EXPECT_TRUE(initial.structure);
    EXPECT_TRUE(initial.changed());
    ASSERT_EQ(rows.rows().size(), 2);
    EXPECT_NE(rows.rows()[0].id, rows.rows()[1].id);
    EXPECT_FALSE(rows.reconcile(playlist, nullptr).changed());

    ASSERT_TRUE(playlist.go_to(1));
    const auto selected = rows.reconcile(playlist, nullptr);
    EXPECT_TRUE(selected.current);
    EXPECT_FALSE(selected.structure);
    EXPECT_TRUE(selected.rows.empty());
    EXPECT_EQ(rows.current(), second);

    media::TrackMetadata updated;
    updated.title = "Changed";
    ASSERT_TRUE(playlist.describe(first, updated));
    const auto patch = rows.reconcile(playlist, nullptr);
    ASSERT_EQ(patch.rows.size(), 1);
    EXPECT_EQ(patch.rows.front().id, gui::TrackRowId{first});
    EXPECT_EQ(rows.rows()[0].metadata.title, "Changed");
    EXPECT_NE(rows.rows()[1].metadata.title, "Changed");
    EXPECT_FALSE(rows.reconcile(playlist, nullptr).changed());

    ASSERT_TRUE(playlist.remove(0));
    EXPECT_TRUE(rows.reconcile(playlist, nullptr).structure);
    ASSERT_EQ(rows.rows().size(), 1);
    EXPECT_EQ(rows.rows()[0].id, gui::TrackRowId{second});

    rows.invalidate();
    EXPECT_TRUE(rows.reconcile(playlist, nullptr).structure);
    engine::Playlist other;
    EXPECT_TRUE(rows.reconcile(other, nullptr).structure);
    EXPECT_TRUE(rows.rows().empty());
    EXPECT_FALSE(rows.current());
}

TEST(QueueRows, CatalogOverridesPreserveQueueDurationArtworkAndPhysicalLocation)
{
    engine::Playlist playlist;
    playlist.set({media::local_resource("actual.flac")});
    const auto id = playlist.tracks()[0].id();
    media::TrackMetadata decoded;
    decoded.title = "Decoded";
    decoded.artist = "Decoder artist";
    decoded.duration = units::Time{42};
    ASSERT_TRUE(playlist.describe(id, decoded));
    auto artwork = std::make_shared<media::Artwork>();
    artwork->data = {std::byte{1}};
    artwork->mime_type = "image/png";
    ASSERT_TRUE(playlist.illustrate(id, artwork));

    library::InMemoryRepository repository;
    library::TrackRecord record;
    record.id = library::MediaId{"catalog"};
    record.locations = {"preferred.flac"};
    record.overrides.title = "Override";
    ASSERT_TRUE(repository.put_track(record));

    gui::QueueRows rows;
    rows.set_resolver([&](const auto&) { return record.id; });
    EXPECT_TRUE(rows.reconcile(playlist, &repository).structure);
    ASSERT_EQ(rows.rows().size(), 1);
    EXPECT_EQ(rows.rows()[0].metadata.title, "Override");
    EXPECT_EQ(rows.rows()[0].metadata.artist, "Decoder artist");
    EXPECT_EQ(rows.rows()[0].metadata.duration, units::Time{42});
    EXPECT_EQ(rows.rows()[0].location, "actual.flac");
    EXPECT_EQ(rows.rows()[0].artwork, playlist.tracks()[0].artwork());
    EXPECT_EQ(rows.rows()[0].artwork.lock(), artwork);

    record.embedded.title = "Embedded";
    ASSERT_TRUE(repository.put_track(record));
    rows.invalidate();
    EXPECT_TRUE(rows.reconcile(playlist, &repository).structure);
    EXPECT_TRUE(rows.rows()[0].metadata.artist.empty());
    EXPECT_EQ(rows.rows()[0].metadata.duration, units::Time{42});

    decoded.title = "Decoder change hidden by override";
    decoded.artist.clear();
    ASSERT_TRUE(playlist.describe(id, decoded));
    EXPECT_FALSE(rows.reconcile(playlist, &repository).changed());
}

TEST(QueueRows, LibraryPatchesUpdateAllDuplicatesWithoutChangingPlaybackSelection)
{
    engine::Playlist playlist;
    playlist.set({media::local_resource("same.flac"), media::local_resource("same.flac")});
    library::InMemoryRepository repository;
    library::TrackRecord record;
    record.id = app::media_id_for("same.flac");
    record.embedded.title = "Original";
    record.locations = {"same.flac"};
    ASSERT_TRUE(repository.put_track(record));
    gui::QueueRows rows;
    EXPECT_TRUE(rows.reconcile(playlist, &repository).structure);
    const auto snapshot = rows.rows();

    record.overrides.title = "Shared change";
    ASSERT_TRUE(repository.put_track(record));
    library::LibraryChangeBatch batch;
    batch.media = {
        {.media = record.id, .fields = library::MediaChangeFields::overrides}
    };
    const auto changes = rows.apply_library(playlist, repository, batch);
    EXPECT_FALSE(changes.structure);
    EXPECT_FALSE(changes.current);
    EXPECT_FALSE(changes.playback_changed);
    EXPECT_TRUE(changes.changed());
    ASSERT_EQ(changes.rows.size(), 2);
    EXPECT_EQ(rows.rows()[0].metadata.title, "Shared change");
    EXPECT_EQ(rows.rows()[1].metadata.title, "Shared change");
    EXPECT_EQ(snapshot[0].metadata.title, "Original");
    EXPECT_FALSE(rows.apply_library(playlist, repository, batch).changed());

    batch.media = {
        {.media = library::MediaId{"unrelated"}, .fields = library::MediaChangeFields::locations}
    };
    EXPECT_FALSE(rows.apply_library(playlist, repository, batch).changed());
    batch.removed_media = {library::MediaId{"unrelated"}};
    EXPECT_FALSE(rows.apply_library(playlist, repository, batch).changed());
}

TEST(QueueRows, DiscoversCatalogIdentityByLocationAndReplacesMediaIndexes)
{
    engine::Playlist playlist;
    playlist.set({media::local_resource("same.flac"), media::local_resource("same.flac")});
    library::InMemoryRepository repository;
    gui::QueueRows rows;
    const library::MediaId catalog{"catalog"};
    rows.set_resolver([&](const auto& path) {
        return repository.track(catalog) ? catalog : app::media_id_for(path);
    });
    EXPECT_TRUE(rows.reconcile(playlist, &repository).structure);

    library::TrackRecord record;
    record.id = catalog;
    record.locations = {"elsewhere.flac", "same.flac"};
    record.embedded.title = "Learned identity";
    ASSERT_TRUE(repository.put_track(record));
    library::LibraryChangeBatch batch;
    batch.media = {
        {.media = catalog, .fields = library::MediaChangeFields::locations}
    };
    const auto learned = rows.apply_library(playlist, repository, batch);
    ASSERT_EQ(learned.rows.size(), 2);
    EXPECT_EQ(rows.rows()[0].media, catalog);
    EXPECT_EQ(rows.rows()[1].media, catalog);

    record.overrides.artist = "Updated by new identity";
    ASSERT_TRUE(repository.put_track(record));
    batch.media = {
        {.media = catalog, .fields = library::MediaChangeFields::overrides}
    };
    EXPECT_EQ(rows.apply_library(playlist, repository, batch).rows.size(), 2);
    EXPECT_EQ(rows.rows()[1].metadata.artist, "Updated by new identity");

    ASSERT_TRUE(repository.replace_all({}, {}, {}));
    batch.media.clear();
    batch.removed_media = {catalog};
    EXPECT_EQ(rows.apply_library(playlist, repository, batch).rows.size(), 2);
    EXPECT_EQ(rows.rows()[0].media, app::media_id_for("same.flac"));
    EXPECT_FALSE(rows.apply_library(playlist, repository, batch).changed());
}

TEST(QueueRows, CoalescesQueueAndLibraryChangesIntoOnePatchPerOccurrence)
{
    engine::Playlist playlist;
    playlist.set({media::local_resource("song.flac")});
    library::InMemoryRepository repository;
    library::TrackRecord record;
    record.id = app::media_id_for("song.flac");
    record.embedded.title = "Catalog";
    ASSERT_TRUE(repository.put_track(record));
    gui::QueueRows rows;
    const auto initial = rows.apply_library(playlist, repository, {});
    EXPECT_TRUE(initial.structure);
    EXPECT_TRUE(initial.playback_changed);

    media::TrackMetadata decoded;
    decoded.duration = units::Time{7};
    ASSERT_TRUE(playlist.describe(playlist.tracks()[0].id(), decoded));
    record.overrides.title = "Updated";
    ASSERT_TRUE(repository.put_track(record));
    library::LibraryChangeBatch batch;
    batch.media = {
        {.media = record.id, .fields = library::MediaChangeFields::all}
    };
    const auto changes = rows.apply_library(playlist, repository, batch);
    EXPECT_TRUE(changes.playback_changed);
    ASSERT_EQ(changes.rows.size(), 1);
    EXPECT_EQ(changes.rows.front().metadata.title, "Updated");
    EXPECT_EQ(changes.rows.front().metadata.duration, units::Time{7});
}

} // namespace
