/**
 * @file
 * @brief Explicit saved-playlist playback queue regression tests.
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

#include <app/playback/playback_queue.hpp>

#include <fakes/output.hpp>
#include <media/local/provider.hpp>
#include <media/local/resource.hpp>

#include <wiola/library/in_memory_repository.hpp>

#include <gtest/gtest.h>

namespace {

TEST(PlaybackQueue, ReplacesTheQueueOnlyThroughAnExplicitSavedPlaylistLoad)
{
    wiola::app::PlaybackService playback{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord media;
    media.id = wiola::library::MediaId{"media-1"};
    media.locations = {"first.wav", "backup.wav"};
    ASSERT_TRUE(repository.put_track(media));

    const auto playlist = repository.create_playlist("Favorites");
    ASSERT_TRUE(repository.append_track(playlist, media.id));
    ASSERT_TRUE(repository.append_track(playlist, media.id));

    wiola::app::PlaybackQueue queue;
    const wiola::app::PlaybackQueueResult result =
        queue.replace_with_saved_playlist(playback, repository, playlist);

    EXPECT_TRUE(result.requested);
    EXPECT_EQ(result.queued, 2);
    EXPECT_EQ(result.unavailable, 0);
    ASSERT_EQ(playback.playlist().tracks().size(), 2);
    EXPECT_EQ(playback.playlist().tracks()[0].resource(),
        wiola::media::local_resource(std::filesystem::path{"first.wav"}));
    EXPECT_EQ(playback.playlist().tracks()[1].resource(),
        wiola::media::local_resource(std::filesystem::path{"first.wav"}));
}

TEST(PlaybackQueue, DoesNotReplaceTheQueueForMissingOrUnavailablePlaylists)
{
    wiola::app::PlaybackService playback{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    playback.add({wiola::media::local_resource("existing.wav")});

    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord unavailable;
    unavailable.id = wiola::library::MediaId{"missing-location"};
    ASSERT_TRUE(repository.put_track(unavailable));

    const auto playlist = repository.create_playlist("Unavailable");
    ASSERT_TRUE(repository.append_track(playlist, unavailable.id));

    wiola::app::PlaybackQueue queue;
    const wiola::app::PlaybackQueueResult unavailable_result =
        queue.replace_with_saved_playlist(playback, repository, playlist);
    const wiola::app::PlaybackQueueResult missing_result =
        queue.replace_with_saved_playlist(playback, repository,
            wiola::library::PlaylistId{"missing"});

    EXPECT_FALSE(unavailable_result.requested);
    EXPECT_EQ(unavailable_result.unavailable, 1);
    EXPECT_EQ(unavailable_result.queued, 0);
    EXPECT_FALSE(missing_result.requested);
    ASSERT_EQ(playback.playlist().tracks().size(), 1);
    EXPECT_EQ(playback.playlist().tracks()[0].resource(),
        wiola::media::local_resource(std::filesystem::path{"existing.wav"}));
}

TEST(PlaybackQueue, AppendsWorkspaceSelectionsWithoutReplacingTheCurrentQueue)
{
    wiola::app::PlaybackService playback{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    playback.add({wiola::media::local_resource("existing.wav")});

    wiola::app::PlaybackQueue queue;
    const auto result = queue.append_paths(playback, {"one.wav", "two.wav"});

    EXPECT_TRUE(result.requested);
    EXPECT_EQ(result.queued, 2);
    ASSERT_EQ(playback.playlist().tracks().size(), 3);
    EXPECT_EQ(playback.playlist().tracks()[0].resource(),
        wiola::media::local_resource(std::filesystem::path{"existing.wav"}));
    EXPECT_EQ(playback.playlist().tracks()[1].resource(),
        wiola::media::local_resource(std::filesystem::path{"one.wav"}));
    EXPECT_EQ(playback.playlist().tracks()[2].resource(),
        wiola::media::local_resource(std::filesystem::path{"two.wav"}));
}

TEST(PlaybackQueue, SelectsTheRequestedDuplicateOccurrenceAndNeverFallsBackForUnknownIds)
{
    wiola::app::PlaybackService playback{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord track;
    track.id = {"same"};
    track.locations = {"duplicate.wav"};
    ASSERT_TRUE(repository.put_track(track));
    const auto playlist = repository.create_playlist("Duplicate");
    const auto first = repository.append_track(playlist, track.id),
               second = repository.append_track(playlist, track.id);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    const auto result = wiola::app::PlaybackQueue{}.replace_with_saved_playlist(playback,
        repository, playlist, *second);
    ASSERT_TRUE(result.requested);
    EXPECT_EQ(playback.playlist().position(), 1);
    const auto before = playback.playlist().tracks()[1].id();
    EXPECT_FALSE(wiola::app::PlaybackQueue{}
            .replace_with_saved_playlist(playback, repository, playlist,
                wiola::library::PlaylistEntryId{"unknown"})
            .requested);
    EXPECT_EQ(playback.playlist().tracks()[1].id(), before);
    EXPECT_EQ(playback.playlist().position(), 1);
}

} // namespace
