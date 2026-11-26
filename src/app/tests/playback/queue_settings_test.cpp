/**
 * @file
 * @brief Portable queue restoration and snapshot storage contracts.
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

#include <app/playback/queue_settings.hpp>
#include <fakes/decoder.hpp>
#include <fakes/output.hpp>
#include <fakes/provider.hpp>
#include <fakes/resource.hpp>
#include <gtest/gtest.h>
#include <stdexcept>
#include <wiola/media/local/resource.hpp>

namespace {

using namespace wiola;

class MemoryQueue final : public app::QueueSettingsStore {
public:
    app::QueueSnapshot read_queue() const override { return snapshot; }

    void write_queue(const app::QueueSnapshot& value) override
    {
        snapshot = value;
        ++writes;
    }

    app::QueueSnapshot snapshot;
    int writes{};
};

class QueueRestoration : public ::testing::Test {
protected:
    app::PlaybackService playback{wiola::testing::passive_output,
        wiola::testing::FakeMediaProvider::decoder_only([](const auto&, const auto&) {
            return media::DecoderOpenResult{std::unique_ptr<media::AudioDecoder>{
                std::make_unique<wiola::testing::FakeDecoder>()}};
        })};
    MemoryQueue store;
    const std::filesystem::path root{std::filesystem::current_path() / "uncreated-queue"};
    const std::filesystem::path first{root / "first.flac"}, second{root / "second.flac"};
};

TEST_F(QueueRestoration, RequiresAnExplicitAvailabilityMechanism)
{
    EXPECT_THROW((app::QueueSettings{playback, store, {}}), std::invalid_argument);
}

TEST_F(QueueRestoration, FiltersMissingTracksAndRemapsTheExactDuplicateOccurrenceAfterExistingRows)
{
    playback.add({wiola::testing::resource("prefix", "Existing")});
    const auto missing = root / "missing.flac";
    store.snapshot.tracks = {first, missing, first};
    store.snapshot.occurrence_ids = {11, 22, 33};
    store.snapshot.selected_occurrence = 33;
    store.snapshot.has_occurrence_ids = true;
    app::QueueSettings settings{playback, store, [&](const auto& path) { return path != missing; }};

    EXPECT_EQ(settings.restore(), 1U);
    ASSERT_EQ(settings.restore_mapping().size(), 2U);
    EXPECT_FALSE(settings.restore_mapping().contains(engine::QueueEntryId{22}));
    EXPECT_NE(settings.restore_mapping().at(engine::QueueEntryId{11}),
        settings.restore_mapping().at(engine::QueueEntryId{33}));
    EXPECT_EQ(playback.playlist().position(), 2U);
    ASSERT_NE(playback.playlist().current(), nullptr);
    EXPECT_EQ(playback.playlist().current()->id(),
        settings.restore_mapping().at(engine::QueueEntryId{33}));
    EXPECT_EQ(store.writes, 0);
}

TEST_F(QueueRestoration, LegacySelectionBelongsToTheAppendedQueueRatherThanItsExistingPrefix)
{
    playback.add({wiola::testing::resource("prefix", "Existing")});
    store.snapshot.tracks = {first, second};
    store.snapshot.selected = second;
    app::QueueSettings settings{playback, store, [](const auto&) { return true; }};

    EXPECT_EQ(settings.restore(), 0U);
    EXPECT_EQ(playback.playlist().position(), 2U);
    EXPECT_TRUE(settings.restore_mapping().empty());
    ASSERT_NE(playback.playlist().current(), nullptr);
    EXPECT_EQ(playback.playlist().current()->resource(), media::local_resource(second));
}

TEST_F(QueueRestoration, InvalidOccurrenceMetadataNeverFallsBackToAnAmbiguousFilename)
{
    store.snapshot.tracks = {first, second, second};
    store.snapshot.selected = second;
    store.snapshot.has_occurrence_ids = true;
    app::QueueSettings settings{playback, store, [](const auto&) { return true; }};

    EXPECT_EQ(settings.restore(), 0U);
    EXPECT_EQ(playback.playlist().position(), 0U);
    EXPECT_TRUE(settings.restore_mapping().empty());
}

TEST_F(QueueRestoration, NoSurvivingTracksLeaveExistingQueueContentsAndClearPreviousRemapping)
{
    store.snapshot.tracks = {first};
    store.snapshot.occurrence_ids = {11};
    store.snapshot.has_occurrence_ids = true;
    bool available = true;
    app::QueueSettings settings{playback, store, [&](const auto&) { return available; }};
    ASSERT_EQ(settings.restore(), 0U);
    ASSERT_EQ(settings.restore_mapping().size(), 1U);
    const auto existing_id = playback.playlist().tracks().front().id();
    available = false;

    EXPECT_EQ(settings.restore(), 1U);
    EXPECT_TRUE(settings.restore_mapping().empty());
    ASSERT_EQ(playback.playlist().tracks().size(), 1U);
    EXPECT_EQ(playback.playlist().tracks().front().id(), existing_id);
}

TEST_F(QueueRestoration, SaveKeepsLocalOccurrencesAndNeverInventsPathsForAnotherProvider)
{
    playback.add({wiola::testing::resource("remote", "Remote"), media::local_resource(first),
        media::local_resource(first)});
    playback.set_repeat(engine::RepeatMode::all);
    playback.shuffle(true);
    app::QueueSettings settings{playback, store, [](const auto&) { return true; }};

    settings.save();

    EXPECT_EQ(store.writes, 1);
    EXPECT_EQ(store.snapshot.tracks, (std::vector{first, first}));
    ASSERT_EQ(store.snapshot.occurrence_ids.size(), 2U);
    EXPECT_NE(store.snapshot.occurrence_ids.front(), store.snapshot.occurrence_ids.back());
    EXPECT_TRUE(store.snapshot.has_occurrence_ids);
    EXPECT_FALSE(store.snapshot.selected);
    EXPECT_FALSE(store.snapshot.selected_occurrence);
    EXPECT_TRUE(store.snapshot.shuffled);
    EXPECT_EQ(store.snapshot.repeat, engine::RepeatMode::all);
}

TEST_F(QueueRestoration, RestorationOwnsItsSnapshotWhileConsultingTheSuppliedDriver)
{
    store.snapshot.tracks = {first, second};
    app::QueueSettings settings{playback, store, [&](const auto&) {
                                    store.snapshot.tracks.clear();
                                    return true;
                                }};
    EXPECT_EQ(settings.restore(), 0U);
    EXPECT_EQ(playback.playlist().tracks().size(), 2U);
}

TEST_F(QueueRestoration, ClosedPlaybackCannotProduceInvalidOccurrenceMappings)
{
    store.snapshot.tracks = {first, second};
    store.snapshot.occurrence_ids = {11, 22};
    store.snapshot.has_occurrence_ids = true;
    app::QueueSettings settings{playback, store, [](const auto&) { return true; }};
    playback.stop_accepting();

    EXPECT_EQ(settings.restore(), store.snapshot.tracks.size());
    EXPECT_TRUE(settings.restore_mapping().empty());
    EXPECT_TRUE(playback.playlist().empty());
}

TEST_F(QueueRestoration, AMissingLegacySelectionLeavesTheDefaultQueueEntrySelected)
{
    store.snapshot.tracks = {first};
    store.snapshot.selected = second;
    app::QueueSettings settings{playback, store, [](const auto&) { return true; }};
    EXPECT_EQ(settings.restore(), 0U);
    EXPECT_EQ(playback.playlist().position(), 0U);
}

TEST_F(QueueRestoration, SavingAnEmptyQueueClearsThePreviousTracksAndSelection)
{
    store.snapshot.tracks = {first};
    store.snapshot.selected = first;
    store.snapshot.occurrence_ids = {11};
    store.snapshot.selected_occurrence = 11;
    store.snapshot.has_occurrence_ids = true;
    app::QueueSettings settings{playback, store, [](const auto&) { return true; }};

    settings.save();

    EXPECT_TRUE(store.snapshot.tracks.empty());
    EXPECT_TRUE(store.snapshot.occurrence_ids.empty());
    EXPECT_FALSE(store.snapshot.selected);
    EXPECT_FALSE(store.snapshot.selected_occurrence);
    EXPECT_TRUE(store.snapshot.has_occurrence_ids);
    EXPECT_EQ(store.writes, 1);
}

} // namespace
