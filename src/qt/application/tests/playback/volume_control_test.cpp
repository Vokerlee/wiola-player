/**
 * @file
 * @brief Unit tests for the volume control.
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

#include <app/playback/volume_control.hpp>
#include <app/playback/volume_scale.hpp>
#include <fakes/output.hpp>
#include <media/local/provider.hpp>
#include <qt/settings.hpp>

#include <QSettings>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cmath>

namespace {

using Control = wiola::app::VolumeControl;
constexpr wiola::app::VolumeScale scale;

/// A settings file of its own, so a test neither reads nor leaves anything of the user's.
class VolumeControl : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(directory.isValid());
        settings.clear();
    }

    int restore(Control& control)
    {
        const wiola::app::PlayerSettings restored{
            wiola::platform::qt::QtSettingsAdapter{settings}.read(control.scale(), {})};
        control.apply_restored(restored.volume_position, restored.boosted);
        return control.position();
    }

    int restore(Control&& control) { return restore(control); }

    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    wiola::platform::qt::QtSettingsAdapter writer{settings};
    wiola::app::PlaybackService volume{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};
};

TEST_F(VolumeControl, StartsAtFullVolume)
{
    Control control{volume, writer};

    EXPECT_EQ(restore(control), scale.full_position);
    EXPECT_EQ(control.position(), scale.full_position);
    EXPECT_FLOAT_EQ(volume.volume(), 1.0F);
}

/// The curve is the point of the control: half the travel is a quarter of the gain, not half.
TEST_F(VolumeControl, AppliesTheCurveToAPosition)
{
    Control control{volume, writer};

    control.set_position(50);

    EXPECT_EQ(control.position(), 50);
    EXPECT_FLOAT_EQ(volume.volume(), 0.25F);
}

TEST_F(VolumeControl, IsSilentAtTheBottomOfTheTravel)
{
    Control control{volume, writer};

    control.set_position(0);

    EXPECT_FLOAT_EQ(volume.volume(), 0.0F);
}

TEST_F(VolumeControl, ClampsAPositionOffTheTravel)
{
    Control control{volume, writer};

    control.set_position(-5);
    EXPECT_EQ(control.position(), 0);
    EXPECT_FLOAT_EQ(volume.volume(), 0.0F);

    control.set_position(500);
    EXPECT_EQ(control.position(), scale.full_position);
    EXPECT_FLOAT_EQ(volume.volume(), 1.0F);
}

/// What the control is for: a run leaves its position for the next one.
TEST_F(VolumeControl, GivesBackWhatTheLastRunLeft)
{
    Control{volume, writer}.set_position(70);

    wiola::app::PlaybackService next{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    Control later{next, writer};

    EXPECT_EQ(restore(later), 70);
    EXPECT_FLOAT_EQ(next.volume(), 0.49F);
}

/// Restoring is applying, not only reading: the volume it was built on is set by it.
TEST_F(VolumeControl, AppliesWhatItRestores)
{
    Control{volume, writer}.set_position(50);

    wiola::app::PlaybackService next{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    EXPECT_FLOAT_EQ(next.volume(), 1.0F);

    Control control{next, writer};
    restore(control);

    EXPECT_FLOAT_EQ(next.volume(), 0.25F);
}

/// The file is text and can be edited, and a typo is not a reason to start silent.
TEST_F(VolumeControl, IgnoresAStoredValueThatIsNotAPosition)
{
    settings.setValue("volume", "loud");

    Control control{volume, writer};

    EXPECT_EQ(restore(control), scale.full_position);
}

TEST_F(VolumeControl, BringsAStoredPositionBackIntoRange)
{
    settings.setValue("volume", 9999);

    EXPECT_EQ(restore(Control(volume, writer)), scale.full_position);

    settings.setValue("volume", -20);

    EXPECT_EQ(restore(Control(volume, writer)), 0);
}

/// Full volume is where the slider stops until the listener asks for more.
TEST_F(VolumeControl, StopsAtFullUntilBoosted)
{
    Control control{volume, writer};

    EXPECT_FALSE(control.boosted());
    EXPECT_EQ(control.max_position(), scale.full_position);

    control.set_position(130);

    EXPECT_EQ(control.position(), scale.full_position);
    EXPECT_FLOAT_EQ(volume.volume(), 1.0F);
}

/// Past full the slider says what it means: a hundred and forty is what arrived and forty more.
TEST_F(VolumeControl, LiftsPastWhatArrivedWhenBoosted)
{
    Control control{volume, writer};

    control.set_boosted(true);

    const int boosted{
        static_cast<int>(std::lround(volume.volume_policy().max_boost * scale.full_position))};
    EXPECT_EQ(control.max_position(), boosted);

    control.set_position(boosted);

    EXPECT_EQ(control.position(), boosted);
    EXPECT_FLOAT_EQ(volume.volume(), 1.4F);
}

/// The curve is below full only, so full is full either way.
TEST_F(VolumeControl, LeavesTheTravelBelowFullAlone)
{
    Control control{volume, writer};

    control.set_boosted(true);
    control.set_position(50);

    EXPECT_FLOAT_EQ(volume.volume(), 0.25F);

    control.set_position(scale.full_position);

    EXPECT_FLOAT_EQ(volume.volume(), 1.0F);
}

/// Turning it off is not a way to keep a position the slider can no longer reach.
TEST_F(VolumeControl, BringsAPositionBackWhenBoostIsTurnedOff)
{
    Control control{volume, writer};

    control.set_boosted(true);
    control.set_position(130);
    control.set_boosted(false);

    EXPECT_EQ(control.position(), scale.full_position);
    EXPECT_FLOAT_EQ(volume.volume(), 1.0F);
}

TEST_F(VolumeControl, GivesBackWhetherItWasBoosted)
{
    Control first{volume, writer};

    first.set_boosted(true);
    first.set_position(120);

    wiola::app::PlaybackService next{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    Control later{next, writer};

    EXPECT_EQ(restore(later), 120);
    EXPECT_TRUE(later.boosted());
    EXPECT_FLOAT_EQ(next.volume(), 1.2F);
}

} // namespace
