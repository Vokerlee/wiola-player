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

#include <app/playback/equalizer_control.hpp>
#include <app/playback/volume_control.hpp>
#include <fakes/output.hpp>
#include <fakes/provider.hpp>
#include <gtest/gtest.h>
#include <map>
#include <stdexcept>

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

} // namespace
