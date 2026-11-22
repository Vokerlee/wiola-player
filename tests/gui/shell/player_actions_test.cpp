/**
 * @file
 * @brief Registration and state tests for shared player actions.
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

#include <gui/shell/player_actions.hpp>

#include <QApplication>
#include <QWidget>

#include <gtest/gtest.h>

namespace {

using wiola::gui::PlayerAction;
using Actions = wiola::gui::PlayerActions;
using Repeat = wiola::engine::Playlist::Repeat;

class PlayerActions : public testing::Test {
protected:
    QWidget styled;
    Actions actions{styled};
};

TEST_F(PlayerActions, RegistersEveryCommandWithALabelAndShortcut)
{
    for (std::size_t index = 0; index < static_cast<std::size_t>(PlayerAction::count); ++index) {
        QAction* action = actions.action(static_cast<PlayerAction>(index));

        ASSERT_NE(action, nullptr) << "action index " << index;
        EXPECT_FALSE(action->text().isEmpty()) << "action index " << index;
        EXPECT_FALSE(action->shortcut().isEmpty()) << "action index " << index;
    }

    EXPECT_EQ(actions.action(PlayerAction::open)->text(), "Open tracks");
    EXPECT_EQ(actions.action(PlayerAction::play)->text(), "Play");
    EXPECT_TRUE(actions.action(PlayerAction::shuffle)->isCheckable());
}

TEST_F(PlayerActions, SharesAvailabilityAndPreventsDisabledActivation)
{
    QAction* play = actions.action(PlayerAction::play);
    int activations{0};

    QObject::connect(play, &QAction::triggered, [&activations] { ++activations; });

    actions.set_available(PlayerAction::play, false);
    play->trigger();

    EXPECT_FALSE(play->isEnabled());
    EXPECT_EQ(activations, 0);

    actions.set_available(PlayerAction::play, true);
    play->trigger();

    EXPECT_TRUE(play->isEnabled());
    EXPECT_EQ(activations, 1);
}

TEST_F(PlayerActions, PresentsPlayPauseAndRepeatState)
{
    actions.set_playing(true);

    EXPECT_EQ(actions.action(PlayerAction::play)->text(), "Pause");
    EXPECT_EQ(actions.action(PlayerAction::play)->toolTip(), "Pause");

    actions.set_playing(false);

    EXPECT_EQ(actions.action(PlayerAction::play)->text(), "Play");

    actions.set_repeat(Repeat::all);
    EXPECT_EQ(actions.action(PlayerAction::repeat)->text(), "Repeat: all");

    actions.set_repeat(Repeat::track);
    EXPECT_EQ(actions.action(PlayerAction::repeat)->text(), "Repeat: one");

    actions.set_repeat(Repeat::none);
    EXPECT_EQ(actions.action(PlayerAction::repeat)->text(), "Repeat: off");
}

TEST_F(PlayerActions, ReflectsShuffleWithoutEmittingANewCommand)
{
    QAction* shuffle = actions.action(PlayerAction::shuffle);
    int toggles{0};

    QObject::connect(shuffle, &QAction::toggled, [&toggles] { ++toggles; });

    actions.set_shuffled(true);

    EXPECT_TRUE(shuffle->isChecked());
    EXPECT_EQ(toggles, 0);

    actions.set_shuffled(false);

    EXPECT_FALSE(shuffle->isChecked());
    EXPECT_EQ(toggles, 0);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
