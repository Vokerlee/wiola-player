/**
 * @file
 * @brief Unit tests for what is to be played and where in it playback has reached.
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

#include <allocation_failure.hpp>
#include <fakes/resource.hpp>
#include <queue/playlist.hpp>

#include <wiola/units.hpp>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using wiola::engine::Playlist;
using wiola::engine::QueueEntry;
using wiola::engine::QueueEntryId;
using wiola::media::TrackMetadata;
using Repeat = wiola::engine::RepeatMode;

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
template<typename Operation>
bool allocation_fails(std::size_t after, Operation operation)
{
    const wiola::testing::AllocationFailure failure{after};

    try {
        operation();
    } catch (const std::bad_alloc&) {
        return true;
    }

    return false;
}

#endif

/// Three tracks, named so that a test can say which one it is standing at.
std::vector<wiola::media::ResourceReference> three()
{
    return {wiola::testing::resource("first.wav", "first"),
        wiola::testing::resource("second.wav", "second"),
        wiola::testing::resource("third.wav", "third")};
}

/// The file a track is, which is what a test says it stands at.
const wiola::media::ResourceReference& resource_of(const Playlist& playlist)
{
    static const wiola::media::ResourceReference none;
    return playlist.current() != nullptr ? playlist.current()->resource() : none;
}

TEST(Playlist, CoalescedResourceMetadataRetainsEachOccurrencesOwnFallbackLabel)
{
    const wiola::media::ResourceReference first{"memory", "same key", "First title"};
    const wiola::media::ResourceReference second{"memory", "same key", "Second title"};
    Playlist queue;
    queue.set({first, second});
    const std::array entries{queue.tracks()[0].id(), queue.tracks()[1].id()};
    queue.apply_metadata(entries, {}, {});
    EXPECT_EQ(queue.tracks()[0].metadata().title, "First title");
    EXPECT_EQ(queue.tracks()[1].metadata().title, "Second title");
    EXPECT_NE(entries[0], entries[1]);
    EXPECT_EQ(queue.tracks()[0].resource(), queue.tracks()[1].resource());
}

} // namespace

TEST(Playlist, StandsNowhereWhileItIsEmpty)
{
    const Playlist playlist;

    EXPECT_TRUE(playlist.empty());
    EXPECT_EQ(playlist.tracks().size(), 0U);
    EXPECT_TRUE(resource_of(playlist).empty());
    EXPECT_FALSE(playlist.position().has_value());
}

TEST(Playlist, GoesNowhereWhileItIsEmpty)
{
    Playlist playlist;

    EXPECT_FALSE(playlist.next());
    EXPECT_FALSE(playlist.previous());
    EXPECT_FALSE(playlist.go_to(0));
}

TEST(Playlist, StandsAtTheFirstOfWhatItIsGiven)
{
    Playlist playlist;

    playlist.set(three());

    EXPECT_EQ(playlist.tracks().size(), 3U);
    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("first.wav", "first"));
    EXPECT_EQ(playlist.position(), 0U);
}

TEST(Playlist, StandsAtTheFirstTrackAdded)
{
    Playlist playlist;

    playlist.add({wiola::testing::resource("only.wav", "only")});
    playlist.add({wiola::testing::resource("after.wav", "after")});

    EXPECT_EQ(playlist.tracks().size(), 2U);
    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("only.wav", "only"));
}

TEST(Playlist, WalksForwardsAndBack)
{
    Playlist playlist;
    playlist.set(three());

    ASSERT_TRUE(playlist.next());
    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("second.wav", "second"));

    ASSERT_TRUE(playlist.next());
    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("third.wav", "third"));

    ASSERT_TRUE(playlist.previous());
    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("second.wav", "second"));
}

/// The end of the list is the end of playback, and it stays where it stopped.
TEST(Playlist, GoesNoFurtherThanTheEnd)
{
    Playlist playlist;
    playlist.set(three());

    ASSERT_TRUE(playlist.go_to(2));

    EXPECT_FALSE(playlist.next());
    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("third.wav", "third"));

    ASSERT_TRUE(playlist.go_to(0));

    EXPECT_FALSE(playlist.previous());
    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("first.wav", "first"));
}

TEST(Playlist, ComesRoundWhenEverythingRepeats)
{
    Playlist playlist;
    playlist.set(three());
    playlist.set_repeat(Repeat::all);

    ASSERT_TRUE(playlist.go_to(2));

    EXPECT_TRUE(playlist.next());
    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("first.wav", "first"));

    EXPECT_TRUE(playlist.previous());
    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("third.wav", "third"));
}

TEST(Playlist, StaysWhereItIsWhenOneTrackRepeats)
{
    Playlist playlist;
    playlist.set(three());
    playlist.set_repeat(Repeat::track);

    ASSERT_TRUE(playlist.go_to(1));

    EXPECT_TRUE(playlist.next());
    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("second.wav", "second"));

    EXPECT_TRUE(playlist.previous());
    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("second.wav", "second"));
}

TEST(Playlist, StandsWhereItIsSent)
{
    Playlist playlist;
    playlist.set(three());

    EXPECT_TRUE(playlist.go_to(2));
    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("third.wav", "third"));
    EXPECT_EQ(playlist.position(), 2U);

    EXPECT_FALSE(playlist.go_to(3));
    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("third.wav", "third"));
}

/// Shuffling changes what comes next, not what is playing now.
TEST(Playlist, KeepsTheTrackItStandsAtWhenShuffled)
{
    Playlist playlist;
    playlist.set(three());

    ASSERT_TRUE(playlist.go_to(1));

    playlist.shuffle(1234);

    EXPECT_TRUE(playlist.shuffled());
    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("second.wav", "second"));
    EXPECT_EQ(playlist.position(), 1U);
}

/// An order, not a fresh choice each time: every track comes once before any comes twice.
TEST(Playlist, ShufflingIsAnOrderOverEveryTrack)
{
    Playlist playlist;
    playlist.set(three());
    playlist.set_repeat(Repeat::none);
    playlist.shuffle(99);

    // Back to the start of the order: shuffling means the first track is not first any more.
    while (playlist.previous()) { }

    std::set<wiola::media::ResourceReference> seen{resource_of(playlist)};

    while (playlist.next())
        seen.insert(resource_of(playlist));

    EXPECT_EQ(seen.size(), 3U);
}

/// The same seed is the same order, so what a listener heard can be heard again.
TEST(Playlist, ShufflesTheSameWayForTheSameSeed)
{
    const auto order_for = [](std::uint_fast32_t seed) {
        Playlist playlist;
        playlist.set(three());
        playlist.shuffle(seed);

        std::vector<wiola::media::ResourceReference> order;

        do
            order.push_back(resource_of(playlist));
        while (playlist.next());

        return order;
    };

    EXPECT_EQ(order_for(7), order_for(7));
}

TEST(Playlist, PutsThemBackInOrder)
{
    Playlist playlist;
    playlist.set(three());
    playlist.shuffle(5);

    ASSERT_TRUE(playlist.go_to(2));

    playlist.unshuffle();

    EXPECT_FALSE(playlist.shuffled());
    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("third.wav", "third"));
    EXPECT_EQ(playlist.position(), 2U);

    ASSERT_TRUE(playlist.go_to(0));
    ASSERT_TRUE(playlist.next());

    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("second.wav", "second"));
}

/// A track nothing is known about is a track with a name: the file's own.
TEST(Playlist, NamesATrackAfterItsFileUntilItIsToldOtherwise)
{
    Playlist playlist;

    playlist.add({wiola::testing::resource("/music/first.wav", "first")});

    ASSERT_NE(playlist.current(), nullptr);
    EXPECT_EQ(playlist.current()->metadata().title, "first") << "a name is not an extension";
    EXPECT_TRUE(playlist.current()->metadata().artist.empty());
    EXPECT_FALSE(playlist.current()->metadata().duration.has_value());
}

/// What is found out about a track later replaces what was assumed, and moves nothing.
TEST(Playlist, TakesWhatIsFoundOutAboutATrack)
{
    Playlist playlist;
    playlist.set(three());

    ASSERT_TRUE(playlist.go_to(1));
    const wiola::engine::QueueEntryId selected{playlist.current()->id()};

    wiola::media::TrackMetadata told;
    told.title = "Together Forever";
    told.artist = "Rick Astley";
    told.album = "Whenever You Need Somebody";
    told.duration = wiola::units::Time{std::chrono::seconds{205}};

    EXPECT_TRUE(playlist.describe(selected, std::move(told)));

    EXPECT_EQ(playlist.current()->metadata().title, "Together Forever");
    EXPECT_EQ(playlist.current()->metadata().artist, "Rick Astley");
    EXPECT_EQ(playlist.position(), 1U) << "saying what a track is moved the list";
    EXPECT_EQ(playlist.tracks().size(), 3U);

    EXPECT_FALSE(playlist.describe(wiola::engine::QueueEntryId{}, {}));
}

/// A window redrawing ten times a second needs to know that something changed without working
/// out what.
TEST(Playlist, CountsEveryChangeToWhatIsQueued)
{
    Playlist playlist;

    const std::size_t empty{playlist.revision()};

    playlist.set(three());
    const std::size_t filled{playlist.revision()};
    EXPECT_NE(filled, empty);

    playlist.add({wiola::testing::resource("fourth.wav", "fourth")});
    const std::size_t added{playlist.revision()};
    EXPECT_NE(added, filled);

    ASSERT_TRUE(playlist.describe(playlist.tracks().front().id(), {}));
    const std::size_t described{playlist.revision()};
    EXPECT_NE(described, added) << "what was found out about a track went unnoticed";

    ASSERT_TRUE(playlist.remove(0));
    EXPECT_NE(playlist.revision(), described);

    playlist.shuffle(11);
    const std::size_t shuffled{playlist.revision()};
    EXPECT_NE(shuffled, described);

    // Standing somewhere else is not a change to what is queued.
    ASSERT_TRUE(playlist.go_to(1));
    EXPECT_EQ(playlist.revision(), shuffled);
}

TEST(Playlist, TakesATrackOut)
{
    Playlist playlist;
    playlist.set(three());

    EXPECT_TRUE(playlist.remove(1));

    ASSERT_EQ(playlist.tracks().size(), 2U);
    EXPECT_EQ(playlist.tracks().front().resource(), wiola::testing::resource("first.wav", "first"));
    EXPECT_EQ(playlist.tracks().back().resource(), wiola::testing::resource("third.wav", "third"));

    EXPECT_FALSE(playlist.remove(2));
}

/// Taking out what came before it does not move what it stands at.
TEST(Playlist, KeepsWhatItStandsAtWhenSomethingBeforeItGoes)
{
    Playlist playlist;
    playlist.set(three());

    ASSERT_TRUE(playlist.go_to(2));
    ASSERT_TRUE(playlist.remove(0));

    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("third.wav", "third"));
    EXPECT_EQ(playlist.position(), 1U);
}

/// Taking out the one it stands at leaves it standing at whatever followed.
TEST(Playlist, StandsAtWhatFollowedTheTrackTakenOut)
{
    Playlist playlist;
    playlist.set(three());

    ASSERT_TRUE(playlist.go_to(1));
    ASSERT_TRUE(playlist.remove(1));

    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("third.wav", "third"));
    EXPECT_EQ(playlist.position(), 1U);
}

/// Nothing followed the last one, so it stands at the end of what is left.
TEST(Playlist, StandsAtTheEndWhenTheLastTrackGoes)
{
    Playlist playlist;
    playlist.set(three());

    ASSERT_TRUE(playlist.go_to(2));
    ASSERT_TRUE(playlist.remove(2));

    EXPECT_EQ(resource_of(playlist), wiola::testing::resource("second.wav", "second"));
    EXPECT_EQ(playlist.position(), 1U);
}

TEST(Playlist, StandsNowhereOnceTheLastTrackIsTakenOut)
{
    Playlist playlist;

    playlist.add({wiola::testing::resource("only.wav", "only")});
    ASSERT_TRUE(playlist.remove(0));

    EXPECT_TRUE(playlist.empty());
    EXPECT_FALSE(playlist.position().has_value());
}

/// A shuffled order holds places in the list, so taking one out has to mend the rest of them.
TEST(Playlist, KeepsAShuffledOrderWholeWhenATrackGoes)
{
    Playlist playlist;
    playlist.set(three());
    playlist.shuffle(3);

    ASSERT_TRUE(playlist.remove(1));
    ASSERT_EQ(playlist.tracks().size(), 2U);

    playlist.set_repeat(Repeat::none);

    // Back to the start of the order, which after shuffling is not the start of the list.
    while (playlist.previous()) { }

    std::set<wiola::media::ResourceReference> seen{resource_of(playlist)};

    while (playlist.next())
        seen.insert(resource_of(playlist));

    EXPECT_EQ(seen.size(), 2U);
    EXPECT_EQ(seen.count(wiola::testing::resource("second.wav", "second")), 0U);
}

TEST(Playlist, StandsNowhereOnceCleared)
{
    Playlist playlist;
    playlist.set(three());

    playlist.clear();

    EXPECT_TRUE(playlist.empty());
    EXPECT_FALSE(playlist.position().has_value());
    EXPECT_TRUE(resource_of(playlist).empty());
}

TEST(Playlist, MakesNoSelectionExplicit)
{
    const Playlist playlist;

    EXPECT_EQ(playlist.current(), nullptr);
}

TEST(Playlist, LiveReadPortTracksMutationsWithoutExposingStorage)
{
    Playlist playlist;
    const wiola::engine::QueueView& view{playlist};
    EXPECT_TRUE(view.empty());
    EXPECT_EQ(view.current(), nullptr);
    EXPECT_FALSE(view.position());

    playlist.set({wiola::testing::resource("same.wav", "same"),
        wiola::testing::resource("same.wav", "same")});
    const auto retained{view.tracks().front()};
    const auto second{view.tracks().back().id()};
    const auto structure{view.structural_revision()};
    EXPECT_EQ(view.index_of(second), 1U);
    EXPECT_EQ(view.current()->id(), retained.id());
    ASSERT_TRUE(playlist.go_to(1));
    EXPECT_EQ(view.position(), 1U);
    EXPECT_EQ(view.current()->id(), second);
    EXPECT_EQ(view.structural_revision(), structure);

    TrackMetadata facts;
    facts.title = "updated";
    const auto metadata{view.metadata_revision()};
    ASSERT_TRUE(playlist.describe(retained.id(), facts));
    EXPECT_EQ(view.metadata_revision(), metadata + 1);
    EXPECT_EQ(view.tracks().front().metadata().title, "updated");
    EXPECT_EQ(retained.metadata().title, "same");

    playlist.add({wiola::testing::resource("third.wav", "third")});
    EXPECT_EQ(view.tracks().size(), 3U);
    EXPECT_NE(view.structural_revision(), structure);
    ASSERT_TRUE(playlist.remove(0));
    EXPECT_FALSE(view.index_of(retained.id()));
    EXPECT_EQ(view.index_of(second), 0U);
    EXPECT_EQ(view.current()->id(), second);
    playlist.clear();
    EXPECT_TRUE(view.empty());
    EXPECT_EQ(view.current(), nullptr);
    EXPECT_FALSE(view.position());
    EXPECT_FALSE(view.index_of(second));
}

TEST(Playlist, GivesDuplicatePathsIndependentIdentities)
{
    Playlist playlist;
    playlist.set({wiola::testing::resource("same.wav", "same"),
        wiola::testing::resource("same.wav", "same")});
    const wiola::engine::QueueEntryId removed{playlist.tracks()[0].id()};
    const wiola::engine::QueueEntryId kept{playlist.tracks()[1].id()};

    EXPECT_NE(removed, kept);
    ASSERT_TRUE(playlist.remove(0));

    wiola::media::TrackMetadata metadata;
    metadata.title = "Only the remaining entry";
    EXPECT_FALSE(playlist.describe(removed, metadata));
    ASSERT_TRUE(playlist.describe(kept, metadata));
    EXPECT_EQ(playlist.tracks().front().metadata().title, "Only the remaining entry");
    EXPECT_EQ(playlist.tracks().front().resource(), wiola::testing::resource("same.wav", "same"));
}

TEST(Playlist, DoesNotReuseIdentitiesAfterClear)
{
    Playlist playlist;
    playlist.add({wiola::testing::resource("before.wav", "before")});
    const wiola::engine::QueueEntryId before{playlist.tracks().front().id()};

    playlist.clear();
    playlist.add({wiola::testing::resource("after.wav", "after")});

    EXPECT_NE(playlist.tracks().front().id(), before);
    EXPECT_FALSE(playlist.describe(before, {}));
}

TEST(Playlist, AppliesOneMetadataBatchWithoutRepeatedQueueRevisions)
{
    Playlist playlist;
    std::vector<wiola::media::ResourceReference> tracks;
    for (std::size_t index = 0; index < 1000; ++index)
        tracks.push_back(wiola::testing::resource("track" + std::to_string(index) + ".wav",
            "Track"));
    playlist.set(std::move(tracks));

    std::vector<QueueEntryId> entries;
    for (const QueueEntry& track : playlist.tracks())
        entries.push_back(track.id());

    const std::size_t revision{playlist.revision()};
    const std::size_t metadata_revision{playlist.metadata_revision()};
    TrackMetadata described;
    described.title = "known";
    playlist.apply_metadata(entries, described, std::make_shared<const wiola::media::Artwork>());

    EXPECT_EQ(playlist.revision(), revision + 1);
    EXPECT_EQ(playlist.metadata_revision(), metadata_revision + 1);
    for (const QueueEntry& track : playlist.tracks())
        EXPECT_EQ(track.metadata().title, "known");
}

TEST(Playlist, IgnoresRemovedIdentitiesInALaterMetadataBatch)
{
    Playlist playlist;
    playlist.set({wiola::testing::resource("first.wav", "first"),
        wiola::testing::resource("second.wav", "second")});
    const QueueEntryId removed{playlist.tracks()[0].id()};
    const QueueEntryId surviving{playlist.tracks()[1].id()};

    ASSERT_TRUE(playlist.remove(0));
    TrackMetadata described;
    described.title = "updated";
    playlist.apply_metadata(std::array{removed, surviving}, described, {});

    ASSERT_EQ(playlist.tracks().size(), 1U);
    EXPECT_EQ(playlist.tracks()[0].id(), surviving);
    EXPECT_EQ(playlist.tracks()[0].metadata().title, "updated");
}

TEST(Playlist, RemovingTheSelectedOccurrenceInvalidatesSelectionEvenAtTheSameRow)
{
    Playlist playlist;
    playlist.set(three());
    ASSERT_TRUE(playlist.go_to(1));
    const auto selected{playlist.current()->id()};
    const auto revision{playlist.selection_revision()};
    ASSERT_TRUE(playlist.remove(1));
    EXPECT_EQ(playlist.position(), 1U);
    EXPECT_NE(playlist.current()->id(), selected);
    EXPECT_EQ(playlist.selection_revision(), revision + 1);
}

TEST(Playlist, RemovingALaterOccurrencePreservesSelectionIdentityAndRevision)
{
    Playlist playlist;
    playlist.set(three());
    const auto selected{playlist.current()->id()};
    const auto revision{playlist.selection_revision()};
    ASSERT_TRUE(playlist.remove(2));
    EXPECT_EQ(playlist.current()->id(), selected);
    EXPECT_EQ(playlist.selection_revision(), revision);
}

TEST(Playlist, EmptyReplacementPreservesRepeatChoiceWithoutInventingASelection)
{
    Playlist playlist;
    playlist.set(three());
    playlist.set_repeat(Repeat::all);
    playlist.set({});
    EXPECT_TRUE(playlist.empty());
    EXPECT_FALSE(playlist.position());
    EXPECT_EQ(playlist.repeat(), Repeat::all);
    playlist.add({wiola::testing::resource("new.wav", "new")});
    EXPECT_EQ(playlist.position(), 0U);
    EXPECT_TRUE(playlist.previous());
    EXPECT_EQ(playlist.position(), 0U);
}

TEST(Playlist, EmptyAppendLeavesTheAcceptedQueueAndRevisionsUntouched)
{
    Playlist playlist;
    playlist.set(three());
    ASSERT_TRUE(playlist.go_to(1));
    const auto revision{playlist.revision()};
    const auto structure{playlist.structural_revision()};
    const auto metadata{playlist.metadata_revision()};
    const auto selection{playlist.selection_revision()};
    const auto id{playlist.current()->id()};
    playlist.add(std::vector<wiola::media::ResourceReference>{});
    EXPECT_EQ(playlist.current()->id(), id);
    EXPECT_EQ(playlist.revision(), revision);
    EXPECT_EQ(playlist.structural_revision(), structure);
    EXPECT_EQ(playlist.metadata_revision(), metadata);
    EXPECT_EQ(playlist.selection_revision(), selection);
}

TEST(Playlist, ArtworkReplacementInvalidatesMetadataWithoutMovingTheSelectedOccurrence)
{
    Playlist playlist;
    playlist.set(three());
    const auto id{playlist.current()->id()};
    const auto revision{playlist.current()->revision()};
    const auto metadata{playlist.metadata_revision()};
    const auto structure{playlist.structural_revision()};
    const auto selection{playlist.selection_revision()};
    auto artwork{std::make_shared<const wiola::media::Artwork>()};
    ASSERT_TRUE(playlist.illustrate(id, artwork));
    EXPECT_EQ(playlist.current()->id(), id);
    EXPECT_EQ(playlist.current()->artwork(), artwork);
    EXPECT_EQ(playlist.current()->revision(), revision + 1);
    EXPECT_EQ(playlist.metadata_revision(), metadata + 1);
    EXPECT_EQ(playlist.structural_revision(), structure);
    EXPECT_EQ(playlist.selection_revision(), selection);
    const auto accepted{playlist.revision()};
    EXPECT_FALSE(playlist.illustrate(QueueEntryId{}, {}));
    EXPECT_EQ(playlist.revision(), accepted);
    EXPECT_EQ(playlist.current()->artwork(), artwork);
}

TEST(Playlist, ObsoleteMetadataBatchesCannotInvalidateLiveQueueObservations)
{
    Playlist playlist;
    playlist.set(three());
    const auto revision{playlist.revision()};
    const auto metadata{playlist.metadata_revision()};
    playlist.apply_metadata(std::array{QueueEntryId{}}, {}, {});
    EXPECT_EQ(playlist.revision(), revision);
    EXPECT_EQ(playlist.metadata_revision(), metadata);
    EXPECT_EQ(playlist.current()->metadata().title, "first");
    playlist.clear();
    const auto selection{playlist.selection_revision()};
    playlist.clear();
    EXPECT_EQ(playlist.selection_revision(), selection);
    EXPECT_EQ(playlist.current(), nullptr);
}

TEST(Playlist, MissingMetadataTitleUsesEachOccurrencesOwnFallback)
{
    Playlist playlist;
    playlist.set(three());
    const std::array entries{playlist.tracks()[0].id(), playlist.tracks()[1].id()};
    const auto revision{playlist.metadata_revision()};
    TrackMetadata observed;
    observed.artist = "Observed artist";
    playlist.apply_metadata(entries, observed, {});

    EXPECT_EQ(playlist.metadata_revision(), revision + 1);
    for (std::size_t index{0}; index < entries.size(); ++index) {
        auto expected{observed};
        expected.title = playlist.tracks()[index].resource().label();
        EXPECT_EQ(playlist.tracks()[index].metadata(), expected);
    }
    EXPECT_EQ(playlist.tracks()[2].metadata().title, "third");
    EXPECT_TRUE(playlist.tracks()[2].metadata().artist.empty());
}

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
TEST(Playlist, MetadataAllocationFailuresPreserveAllOccurrencesAndRevisions)
{
    bool observed_failure{false};
    for (std::size_t after{0};; ++after) {
        Playlist playlist;
        playlist.set(three());
        ASSERT_TRUE(playlist.go_to(1));
        const std::vector<QueueEntry> accepted{playlist.tracks().begin(), playlist.tracks().end()};
        const std::array entries{accepted[0].id(), accepted[1].id(), accepted[2].id()};
        const auto revision{playlist.revision()};
        const auto metadata_revision{playlist.metadata_revision()};
        const auto structural_revision{playlist.structural_revision()};
        const auto selection_revision{playlist.selection_revision()};
        auto artwork{std::make_shared<const wiola::media::Artwork>()};
        TrackMetadata observed;
        observed.artist.assign(256, 'a');
        observed.comment.assign(256, 'c');

        if (!allocation_fails(after,
                [&] { playlist.apply_metadata(entries, observed, artwork); })) {
            EXPECT_TRUE(observed_failure);
            EXPECT_EQ(playlist.revision(), revision + 1);
            EXPECT_EQ(playlist.metadata_revision(), metadata_revision + 1);
            for (std::size_t index{0}; index < accepted.size(); ++index) {
                auto expected{observed};
                expected.title = accepted[index].metadata().title;
                EXPECT_EQ(playlist.tracks()[index].metadata(), expected);
                EXPECT_EQ(playlist.tracks()[index].artwork(), artwork);
                EXPECT_EQ(playlist.tracks()[index].revision(), accepted[index].revision() + 1);
            }
            break;
        }
        observed_failure = true;
        EXPECT_EQ(playlist.revision(), revision);
        EXPECT_EQ(playlist.metadata_revision(), metadata_revision);
        EXPECT_EQ(playlist.structural_revision(), structural_revision);
        EXPECT_EQ(playlist.selection_revision(), selection_revision);
        EXPECT_EQ(playlist.position(), 1U);
        ASSERT_EQ(playlist.tracks().size(), accepted.size());
        for (std::size_t index{0}; index < accepted.size(); ++index) {
            EXPECT_EQ(playlist.tracks()[index].id(), accepted[index].id());
            EXPECT_EQ(playlist.tracks()[index].metadata(), accepted[index].metadata());
            EXPECT_EQ(playlist.tracks()[index].artwork(), accepted[index].artwork());
            EXPECT_EQ(playlist.tracks()[index].revision(), accepted[index].revision());
        }
    }
}

TEST(Playlist, ReplacementAllocationFailuresLeaveTheAcceptedQueueUntouched)
{
    bool observed_failure{false};
    for (std::size_t after{0};; ++after) {
        Playlist playlist;
        playlist.set(three());
        playlist.go_to(1);
        const std::vector ids{playlist.tracks()[0].id(), playlist.tracks()[1].id(),
            playlist.tracks()[2].id()};
        const std::size_t revision{playlist.revision()};
        const std::size_t structural_revision{playlist.structural_revision()};
        std::vector<wiola::media::ResourceReference> replacement{
            wiola::testing::resource("new-first.wav", "new-first"),
            wiola::testing::resource("new-second.wav", "new-second")};

        if (!allocation_fails(after, [&] { playlist.set(std::move(replacement)); })) {
            EXPECT_TRUE(observed_failure);
            ASSERT_EQ(playlist.tracks().size(), 2U);
            EXPECT_EQ(playlist.tracks()[0].id(), QueueEntryId{4});
            EXPECT_EQ(playlist.tracks()[1].id(), QueueEntryId{5});
            break;
        }
        observed_failure = true;

        ASSERT_EQ(playlist.tracks().size(), 3U);
        EXPECT_EQ(playlist.revision(), revision);
        EXPECT_EQ(playlist.structural_revision(), structural_revision);
        EXPECT_EQ(resource_of(playlist), wiola::testing::resource("second.wav", "second"));

        for (std::size_t index{0}; index < ids.size(); ++index) {
            EXPECT_EQ(playlist.tracks()[index].id(), ids[index]);
            EXPECT_EQ(playlist.index_of(ids[index]), index);
        }

        playlist.add({wiola::testing::resource("accepted.wav", "accepted")});
        EXPECT_EQ(playlist.tracks().back().id(), QueueEntryId{4});
    }
}

TEST(Playlist, AppendAllocationFailuresLeaveTheAcceptedQueueUntouched)
{
    bool observed_failure{false};
    for (std::size_t after{0};; ++after) {
        Playlist playlist;
        playlist.set(three());
        const std::vector ids{playlist.tracks()[0].id(), playlist.tracks()[1].id(),
            playlist.tracks()[2].id()};
        const std::size_t revision{playlist.revision()};
        const std::size_t structural_revision{playlist.structural_revision()};
        std::vector<wiola::media::ResourceReference> additions{
            wiola::testing::resource("fourth.wav", "fourth"),
            wiola::testing::resource("fifth.wav", "fifth")};

        if (!allocation_fails(after, [&] { playlist.add(std::move(additions)); })) {
            EXPECT_TRUE(observed_failure);
            ASSERT_EQ(playlist.tracks().size(), 5U);
            EXPECT_EQ(playlist.tracks()[3].id(), QueueEntryId{4});
            EXPECT_EQ(playlist.tracks()[4].id(), QueueEntryId{5});
            break;
        }
        observed_failure = true;

        ASSERT_EQ(playlist.tracks().size(), 3U);
        EXPECT_EQ(playlist.revision(), revision);
        EXPECT_EQ(playlist.structural_revision(), structural_revision);

        for (std::size_t index{0}; index < ids.size(); ++index) {
            EXPECT_EQ(playlist.tracks()[index].id(), ids[index]);
            EXPECT_EQ(playlist.index_of(ids[index]), index);
        }

        playlist.add({wiola::testing::resource("fourth.wav", "fourth"),
            wiola::testing::resource("fifth.wav", "fifth")});
        EXPECT_EQ(playlist.tracks()[3].id(), QueueEntryId{4});
        EXPECT_EQ(playlist.tracks()[4].id(), QueueEntryId{5});
        EXPECT_EQ(playlist.index_of(playlist.tracks()[4].id()), 4U);
    }
}

#endif

static_assert(noexcept(std::declval<Playlist&>().remove(0)));
