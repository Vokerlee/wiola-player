/**
 * @file
 * @brief Transactional playlist transfer regression tests.
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

#include <app/playlist_transfer.hpp>

#include <gtest/gtest.h>

namespace {

TEST(PlaylistTransfer, AddsSelectedMediaToTheActivePlaylistWithDuplicateOccurrences)
{
    wiola::library::InMemoryRepository repository;
    wiola::app::ActivePlaylist active;
    const auto destination = repository.create_playlist("Destination");
    ASSERT_TRUE(active.set(destination));

    const wiola::library::MediaId media{"media-1"};
    const wiola::app::MediaTransfer item{.media = media, .location = "/music/track.flac"};
    wiola::app::PlaylistTransfer transfer{repository, active,
        [](const wiola::library::LibraryRepository&) { return true; }};

    const auto result = transfer.add_to_active({item, item});

    EXPECT_TRUE(result.succeeded);
    EXPECT_EQ(result.added, 2);
    ASSERT_NE(repository.track(media), nullptr);
    EXPECT_EQ(repository.track(media)->locations.front(), item.location);

    const auto* saved = repository.playlist(destination);
    ASSERT_NE(saved, nullptr);
    ASSERT_EQ(saved->entries.size(), 2);
    EXPECT_NE(saved->entries[0].id, saved->entries[1].id);
    EXPECT_EQ(saved->entries[0].media, media);
    EXPECT_EQ(saved->entries[1].media, media);
}

TEST(PlaylistTransfer, SupportsExplicitDestinationsAndRollsBackFailedBatches)
{
    wiola::library::InMemoryRepository repository;
    wiola::app::ActivePlaylist active;
    const auto active_id = repository.create_playlist("Active");
    const auto other_id = repository.create_playlist("Other");
    ASSERT_TRUE(active.set(active_id));

    const wiola::library::MediaId media{"media-1"};
    const wiola::app::MediaTransfer item{.media = media, .location = "/music/track.flac"};
    wiola::app::PlaylistTransfer transfer{repository, active,
        [](const wiola::library::LibraryRepository&) { return true; }};

    EXPECT_TRUE(transfer.add_to(other_id, {item}).succeeded);
    EXPECT_EQ(active.id(), active_id);
    ASSERT_NE(repository.playlist(other_id), nullptr);
    EXPECT_EQ(repository.playlist(other_id)->entries.size(), 1);

    const wiola::library::MediaId rejected{"media-rejected"};
    wiola::app::PlaylistTransfer failing{repository, active,
        [](const wiola::library::LibraryRepository&) { return false; }};
    const auto result = failing.add_to_active({
        {.media = rejected, .location = "rejected.wav"}
    });

    EXPECT_FALSE(result.succeeded);
    EXPECT_EQ(result.added, 0);
    EXPECT_EQ(repository.track(rejected), nullptr);
    EXPECT_EQ(repository.playlist(active_id)->entries.size(), 0);
    EXPECT_EQ(repository.playlist(other_id)->entries.size(), 1);
}

} // namespace
