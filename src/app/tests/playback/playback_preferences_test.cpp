/**
 * @file
 * @brief Portable playback controls and typed settings writes.
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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fakes/decoder.hpp>
#include <fakes/output.hpp>
#include <fakes/provider.hpp>
#include <gtest/gtest.h>
#include <map>
#include <mutex>
#include <semaphore>
#include <stdexcept>
#include <thread>
#include <wiola/app/playback/equalizer_control.hpp>
#include <wiola/app/playback/volume_control.hpp>

#if defined(QT_CORE_LIB) || defined(QT_GUI_LIB) || defined(QT_WIDGETS_LIB)
#error Portable playback composition must not depend on Qt.
#endif

namespace {

using namespace wiola;

class CapturedSettings final : public app::PlaybackSettingsWriter {
public:
    void write_volume_position(int position) override
    {
        writing();
        values.volume_position = position;
    }

    void write_boosted(bool boosted) override
    {
        writing();
        values.boosted = boosted;
    }

    void write_equalizer_enabled(bool enabled) override
    {
        writing();
        values.equalizer_enabled = enabled;
    }

    void write_band_gain(std::size_t index, float db) override
    {
        writing();
        bands[index] = db;
    }

    app::PlayerSettings values;
    std::map<std::size_t, float> bands;
    std::size_t writes{};
    bool fail{};

private:
    void writing()
    {
        if (fail)
            throw std::logic_error{"store unavailable"};
        ++writes;
    }
};

class PlaybackPreferencesPolicy : public ::testing::Test {
protected:
    app::PlaybackService playback{wiola::testing::passive_output,
        wiola::testing::FakeMediaProvider::metadata_only([](const auto&, const auto&) {
            return media::Result<media::Metadata>{media::Metadata{}};
        })};
    CapturedSettings store;
};

TEST_F(PlaybackPreferencesPolicy, StartupAppliesAllSettingsWithoutWritingThemBack)
{
    app::VolumeControl volume{playback, store};
    app::EqualizerControl equalizer{playback, store};
    volume.apply_restored(volume.scale().full_position / 2, true);
    const std::vector gains{3.0F};
    equalizer.apply_restored(false, gains);
    EXPECT_FLOAT_EQ(playback.volume(), 0.25F);
    EXPECT_TRUE(volume.boosted());
    EXPECT_FALSE(equalizer.enabled());
    EXPECT_FLOAT_EQ(equalizer.band_gain(0), gains.front());
    for (std::size_t index = 1; index < playback.equalizer_config().layout.count; ++index)
        EXPECT_FLOAT_EQ(equalizer.band_gain(index), 0.0F);
    EXPECT_EQ(store.writes, 0U);
}

TEST_F(PlaybackPreferencesPolicy, TheStoreReceivesTheClampedVolumeAndBoostState)
{
    app::VolumeControl volume{playback, store};
    volume.set_boosted(true);
    volume.set_position(volume.max_position());
    EXPECT_GT(store.values.volume_position, volume.scale().full_position);
    volume.set_boosted(false);
    EXPECT_FALSE(store.values.boosted);
    EXPECT_EQ(store.values.volume_position, volume.scale().full_position);
    EXPECT_FLOAT_EQ(playback.volume(), 1.0F);
    volume.set_position(-1);
    EXPECT_EQ(store.values.volume_position, 0);
    EXPECT_FLOAT_EQ(playback.volume(), 0.0F);
}

TEST_F(PlaybackPreferencesPolicy, TheStoreReceivesTheGainAcceptedByTheAudioPolicy)
{
    app::EqualizerControl equalizer{playback, store};
    equalizer.set_enabled(false);
    EXPECT_FALSE(store.values.equalizer_enabled);
    equalizer.set_band_gain(0, equalizer.max_band_gain() * 2);
    ASSERT_EQ(store.bands.size(), 1U);
    EXPECT_FLOAT_EQ(store.bands.at(0), equalizer.max_band_gain());
    EXPECT_FLOAT_EQ(store.bands.at(0), playback.equalizer_band(0));
}

TEST_F(PlaybackPreferencesPolicy, AnUnknownBandNeverCreatesAPersistedSetting)
{
    app::EqualizerControl equalizer{playback, store};
    equalizer.set_band_gain(playback.equalizer_config().layout.count, 2.0F);
    EXPECT_TRUE(store.bands.empty());
    EXPECT_EQ(store.writes, 0U);
}

TEST_F(PlaybackPreferencesPolicy, AStoreFailureDoesNotRollBackAlreadyAppliedAudioSettings)
{
    app::VolumeControl volume{playback, store};
    app::EqualizerControl equalizer{playback, store};
    store.fail = true;
    EXPECT_THROW(volume.set_position(0), std::logic_error);
    EXPECT_FLOAT_EQ(playback.volume(), 0.0F);
    EXPECT_THROW(equalizer.set_enabled(false), std::logic_error);
    EXPECT_FALSE(playback.equalizer_enabled());
    store.fail = false;
    volume.set_position(volume.scale().full_position);
    equalizer.set_enabled(true);
    EXPECT_FLOAT_EQ(playback.volume(), 1.0F);
    EXPECT_TRUE(store.values.equalizer_enabled);
}

TEST_F(PlaybackPreferencesPolicy, StartupClampsAnUnboundedRestoredPositionToTheActualTravel)
{
    app::VolumeControl volume{playback, store};
    volume.apply_restored(volume.scale().full_position * 100, false);
    EXPECT_EQ(volume.position(), volume.max_position());
    volume.apply_restored(-1, true);
    EXPECT_EQ(volume.position(), 0);
    EXPECT_EQ(store.writes, 0U);
}

TEST_F(PlaybackPreferencesPolicy, FailedBoostStorageStillLeavesRuntimeVolumeWithinItsNewBounds)
{
    app::VolumeControl volume{playback, store};
    volume.set_boosted(true);
    volume.set_position(volume.max_position());
    ASSERT_GT(volume.position(), volume.scale().full_position);
    store.fail = true;

    EXPECT_THROW(volume.set_boosted(false), std::logic_error);

    EXPECT_FALSE(volume.boosted());
    EXPECT_EQ(volume.position(), volume.scale().full_position);
    EXPECT_FLOAT_EQ(playback.volume(), 1.0F);
}

template<typename Ready>
bool wait_for(Ready ready)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!ready() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    return ready();
}

bool complete_open(app::PlaybackService& playback)
{
    if (!wait_for([&] { return !playback.reading(); }))
        return false;
    playback.pump();
    return true;
}

TEST(PlaybackComposition, ProviderNamespacesRemainDistinctAcrossLoadsAndPersistedControls)
{
    const media::ResourceReference first{"driver-a", "opaque:42/../track?key=%2F", "First"};
    const media::ResourceReference second{"driver-b", first.key(), "Second"};
    CapturedSettings store;
    std::mutex decoded_mutex;
    std::vector<media::ResourceReference> decoded;
    std::weak_ptr<media::MediaProvider> retained_provider;
    {
        auto provider = std::make_shared<wiola::testing::FakeMediaProvider>(
            [&](const media::ResourceReference& resource,
                const media::OperationContext&) -> media::DecoderOpenResult {
                const std::lock_guard lock{decoded_mutex};
                decoded.push_back(resource);
                return std::unique_ptr<media::AudioDecoder>{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const media::ResourceReference& resource, const media::OperationContext&) {
                media::Metadata metadata;
                metadata.track.title = resource.label();
                return metadata;
            }
        );
        retained_provider = provider;
        wiola::testing::FakeOutput* device{};
        app::PlaybackService playback{[&](audio::Source&) {
                                          auto output =
                                              std::make_unique<wiola::testing::FakeOutput>();
                                          device = output.get();
                                          return output;
                                      },
            provider};
        provider.reset();
        app::VolumeControl volume{playback, store};
        app::EqualizerControl equalizer{playback, store};
        const std::vector restored_gains{2.0F};
        volume.apply_restored(volume.scale().full_position / 2, false);
        equalizer.apply_restored(false, restored_gains);
        EXPECT_EQ(store.writes, 0U);

        const auto accepted = playback.open({first, second});
        ASSERT_EQ(accepted.acceptance, engine::CommandResult::accepted);
        ASSERT_TRUE(complete_open(playback));
        ASSERT_TRUE(playback.loaded());
        EXPECT_EQ(playback.track(), first);
        ASSERT_NE(device, nullptr);
        EXPECT_FLOAT_EQ(playback.volume(), 0.25F);
        EXPECT_FALSE(playback.equalizer_enabled());
        EXPECT_FLOAT_EQ(playback.equalizer_band(0), restored_gains.front());
        ASSERT_EQ(playback.play(), engine::CommandResult::accepted);
        ASSERT_TRUE(wait_for([&] { return device->num_starts() > 0; }));
        ASSERT_EQ(playback.pause(), engine::CommandResult::accepted);

        volume.set_position(volume.scale().full_position / 4);
        equalizer.set_enabled(true);
        equalizer.set_band_gain(0, 3.0F);
        ASSERT_TRUE(playback.play_track(1));
        ASSERT_TRUE(complete_open(playback));
        ASSERT_TRUE(playback.loaded());
        EXPECT_EQ(playback.track(), second);
        EXPECT_EQ(playback.track().label(), second.label());
        EXPECT_FLOAT_EQ(playback.volume(), volume.scale().gain_of(store.values.volume_position));
        EXPECT_EQ(playback.equalizer_enabled(), store.values.equalizer_enabled);
        EXPECT_FLOAT_EQ(playback.equalizer_band(0), store.bands.at(0));
        EXPECT_FALSE(retained_provider.expired());

        playback.stop_accepting();
        EXPECT_EQ(playback.open({first}).acceptance, engine::CommandResult::unavailable);
        EXPECT_EQ(playback.track(), second);
    }
    EXPECT_TRUE(retained_provider.expired());
    const std::lock_guard lock{decoded_mutex};
    ASSERT_EQ(decoded, (std::vector{first, second}));
    EXPECT_EQ(decoded.front().label(), first.label());
    EXPECT_EQ(decoded.back().label(), second.label());
}

TEST(PlaybackComposition, FailureDiagnosticsOutliveTheirProviderAndService)
{
    engine::OperationOutcomes outcomes;
    engine::OperationReceipt accepted;
    std::weak_ptr<media::MediaProvider> retained_provider;
    const std::string diagnostic{"provider-owned diagnostic with opaque key: remote:42"};
    {
        auto provider = wiola::testing::FakeMediaProvider::decoder_only(
            [detail = diagnostic](const media::ResourceReference&,
                const media::OperationContext&) -> media::DecoderOpenResult {
                return media::unexpected{
                    media::Error{.category = media::ErrorCategory::unreadable, .detail = detail}
                };
            }
        );
        retained_provider = provider;
        app::PlaybackService playback{wiola::testing::passive_output, provider};
        provider.reset();
        accepted = playback.open({
            media::ResourceReference{"custom", "remote:42", "Remote"}
        });
        ASSERT_EQ(accepted.acceptance, engine::CommandResult::accepted);
        ASSERT_TRUE(complete_open(playback));
        EXPECT_FALSE(playback.loaded());
        outcomes = playback.take_outcomes();
    }
    EXPECT_TRUE(retained_provider.expired());
    const auto failure = std::ranges::find_if(outcomes.values, [&](const auto& outcome) {
        return outcome.operation == engine::OperationKind::open && outcome.id == accepted.id;
    });
    ASSERT_NE(failure, outcomes.values.end());
    ASSERT_TRUE(failure->error);
    const auto* error = std::get_if<media::Error>(&*failure->error);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->category, media::ErrorCategory::unreadable);
    EXPECT_EQ(error->detail, diagnostic);
}

TEST(PlaybackComposition, DestructionCancelsAndJoinsASuppliedProviderBeforeReleasingIt)
{
    std::binary_semaphore entered{0};
    std::mutex mutex;
    std::condition_variable_any waiting;
    std::atomic<bool> cancelled{false};
    std::weak_ptr<media::MediaProvider> retained_provider;
    {
        auto provider = wiola::testing::FakeMediaProvider::decoder_only(
            [&](const media::ResourceReference&,
                const media::OperationContext& context) -> media::DecoderOpenResult {
                std::unique_lock lock{mutex};
                entered.release();
                waiting.wait(lock, context.stop, [] { return false; });
                cancelled.store(context.stop.stop_requested());
                return media::unexpected{context.error()};
            }
        );
        retained_provider = provider;
        app::PlaybackService playback{wiola::testing::passive_output, provider};
        provider.reset();
        const auto accepted = playback.open({
            media::ResourceReference{"custom", "pending", "Pending"}
        });
        ASSERT_EQ(accepted.acceptance, engine::CommandResult::accepted);
        ASSERT_TRUE(entered.try_acquire_for(std::chrono::seconds{5}));
        EXPECT_FALSE(cancelled.load());
    }
    EXPECT_TRUE(cancelled.load());
    EXPECT_TRUE(retained_provider.expired());
}

} // namespace
