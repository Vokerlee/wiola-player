/**
 * @file
 * @brief Unit tests for the equalizer control.
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

#include <fakes/decoder.hpp>
#include <fakes/output.hpp>
#include <fakes/provider.hpp>
#include <wiola/app/playback/equalizer_control.hpp>
#include <wiola/media/stream_spec.hpp>
#include <wiola/qt/storage/settings.hpp>
#include <wiola/units.hpp>

#include <QSettings>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <thread>

namespace {

using namespace wiola::units::literals;
using Control = wiola::app::EqualizerControl;
using wiola::pcm::StreamSpec;

/// A settings file of its own, so a test neither reads nor leaves anything of the user's.
class EqualizerControl : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(directory.isValid());
        settings.clear();
    }

    void restore(Control& control)
    {
        const wiola::app::PlayerSettings restored{
            wiola::platform::qt::QtSettingsAdapter{settings}.read({},
                equalizer.equalizer_config())};
        control.apply_restored(restored.equalizer_enabled, restored.band_gains);
    }

    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    wiola::platform::qt::QtSettingsAdapter writer{settings};
    wiola::app::PlaybackService equalizer{wiola::testing::passive_output,
        wiola::testing::FakeMediaProvider::unavailable()};
};

TEST_F(EqualizerControl, StartsFlatAndOn)
{
    Control control{equalizer, writer};

    restore(control);

    EXPECT_TRUE(control.enabled());
    EXPECT_FLOAT_EQ(control.preamp(), 0.0F);

    for (std::size_t index = 0; index < control.num_bands(); ++index)
        EXPECT_FLOAT_EQ(control.band_gain(index), 0.0F) << "band " << index;
}

TEST_F(EqualizerControl, GivesBackWhatTheLastRunLeft)
{
    Control{equalizer, writer}.set_band_gain(2, 6.0F);

    wiola::app::PlaybackService next{wiola::testing::passive_output,
        wiola::testing::FakeMediaProvider::unavailable()};

    Control later{next, writer};

    restore(later);

    EXPECT_FLOAT_EQ(later.band_gain(2), 6.0F);
}

TEST_F(EqualizerControl, KeepsWhetherItRuns)
{
    Control control{equalizer, writer};

    control.set_enabled(false);

    wiola::app::PlaybackService next{wiola::testing::passive_output,
        wiola::testing::FakeMediaProvider::unavailable()};

    Control later{next, writer};

    restore(later);

    EXPECT_FALSE(later.enabled());
}

/// What is stored is what was applied: the equalizer decides what it allows, not the file.
TEST_F(EqualizerControl, KeepsWhatTheEqualizerAllowed)
{
    Control control{equalizer, writer};
    const float limit{control.max_band_gain()};

    control.set_band_gain(0, 99.0F);

    EXPECT_FLOAT_EQ(control.band_gain(0), limit);

    wiola::app::PlaybackService next{wiola::testing::passive_output,
        wiola::testing::FakeMediaProvider::unavailable()};

    Control later{next, writer};

    restore(later);

    EXPECT_FLOAT_EQ(later.band_gain(0), limit);
}

/// A band this format cannot carry still has a setting, so it is there for the track that can.
TEST_F(EqualizerControl, KeepsABandTheFormatCannotCarry)
{
    wiola::app::PlaybackService narrow{wiola::testing::passive_output,
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>(wiola::pcm::Frames{64},
                        StreamSpec{.sample_rate = 8_kHz, .num_channels = 2})};
            },
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
        )};
    narrow.open({
        wiola::media::ResourceReference{"controlled", "narrow-stream", "Narrow stream"}
    });

    while (narrow.reading())
        std::this_thread::yield();

    static_cast<void>(narrow.pump());
    ASSERT_EQ(narrow.open_result(), wiola::engine::OpenResult::opened);

    Control control{narrow, writer};

    const std::size_t beyond{narrow.num_equalizer_bands()};

    ASSERT_LT(beyond, narrow.equalizer_config().layout.count);

    control.set_band_gain(beyond, 5.0F);

    wiola::app::PlaybackService wide{wiola::testing::passive_output,
        wiola::testing::FakeMediaProvider::unavailable()};

    Control later{wide, writer};

    restore(later);

    ASSERT_GT(wide.num_equalizer_bands(), beyond);
    EXPECT_FLOAT_EQ(later.band_gain(beyond), 5.0F);
}

/// The file is text and can be edited, and a typo leaves the band flat rather than anywhere.
TEST_F(EqualizerControl, IgnoresAStoredValueThatIsNotAGain)
{
    settings.setValue("equalizer/band/1", "high");

    Control control{equalizer, writer};

    restore(control);

    EXPECT_FLOAT_EQ(control.band_gain(1), 0.0F);
}

TEST_F(EqualizerControl, RestoresEveryBandTheLayoutNames)
{
    Control control{equalizer, writer};

    for (std::size_t index = 0; index < equalizer.equalizer_config().layout.count; ++index)
        control.set_band_gain(index, static_cast<float>(index) - 5.0F);

    wiola::app::PlaybackService next{wiola::testing::passive_output,
        wiola::testing::FakeMediaProvider::unavailable()};

    Control later{next, writer};

    restore(later);

    for (std::size_t index = 0; index < equalizer.equalizer_config().layout.count; ++index)
        EXPECT_FLOAT_EQ(later.band_gain(index), static_cast<float>(index) - 5.0F)
            << "band " << index;
}

TEST_F(EqualizerControl, IgnoresANonFiniteStoredGain)
{
    settings.setValue("equalizer/band/1", std::numeric_limits<float>::infinity());

    Control control{equalizer, writer};

    restore(control);

    EXPECT_FLOAT_EQ(control.band_gain(1), 0.0F);
}

} // namespace
