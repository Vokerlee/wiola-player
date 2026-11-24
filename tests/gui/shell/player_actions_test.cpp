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

#include <QPushButton>
#include <gui/shell/action_button_binding.hpp>
#include <gui/shell/player_actions.hpp>

#include <QApplication>
#include <QWidget>
#include <fixtures/gui_interaction.hpp>

#include <gtest/gtest.h>

namespace {

using wiola::gui::PlayerAction;
using Actions = wiola::gui::PlayerActions;
using Repeat = wiola::engine::RepeatMode;

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

TEST_F(PlayerActions, SpaceActivatesTheFocusedButtonAndRemainsAPlayerShortcutElsewhere)
{
    QPushButton reset{"Reset", &styled};
    styled.setFocusPolicy(Qt::StrongFocus);
    styled.addAction(actions.action(PlayerAction::play));
    int resets{}, plays{};
    QObject::connect(&reset, &QPushButton::clicked, [&] { ++resets; });
    QObject::connect(actions.action(PlayerAction::play), &QAction::triggered, [&] { ++plays; });
    styled.show();
    styled.activateWindow();
    reset.setFocus();
    ASSERT_TRUE(wiola::testing::await_gui([&] { return reset.hasFocus(); }));
    QTest::keyClick(&reset, Qt::Key_Space);
    EXPECT_EQ(resets, 1);
    EXPECT_EQ(plays, 0);
    styled.setFocus();
    ASSERT_TRUE(wiola::testing::await_gui([&] { return styled.hasFocus(); }));
    QTest::keyClick(&styled, Qt::Key_Space);
    EXPECT_EQ(plays, 1);
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

TEST_F(PlayerActions, EveryButtonTracksSharedStateAndDisconnectsWithItsLifetime)
{
    QPushButton first, second;
    auto* command = actions.action(wiola::gui::PlayerAction::shuffle);
    new wiola::gui::ActionButtonBinding{first, *command};
    new wiola::gui::ActionButtonBinding{second, *command};
    int triggered{};
    QObject::connect(command, &QAction::triggered, [&] { ++triggered; });
    actions.set_shuffled(true);
    EXPECT_TRUE(first.isChecked());
    EXPECT_TRUE(second.isChecked());
    EXPECT_EQ(triggered, 0);
    first.click();
    EXPECT_EQ(triggered, 1);
    EXPECT_FALSE(first.isChecked());
    EXPECT_FALSE(second.isChecked());
    command->setText("A shared label");
    command->setToolTip("Shared help");
    command->setEnabled(false);
    EXPECT_EQ(first.text(), second.text());
    EXPECT_EQ(first.accessibleName(), "A shared label");
    EXPECT_EQ(second.toolTip(), "Shared help");
    EXPECT_FALSE(first.isEnabled());
    for (int i = 0; i < 10; ++i) {
        QPushButton temporary;
        new wiola::gui::ActionButtonBinding{temporary, *command};
    }
    command->setEnabled(true);
    second.click();
    EXPECT_EQ(triggered, 2);
}

TEST_F(PlayerActions, BindingSafelyDisablesWhenTheActionDies)
{
    QPushButton button;
    auto* command = new QAction{"Temporary"};
    new wiola::gui::ActionButtonBinding{button, *command,
        wiola::gui::ActionButtonPresentation::icon_only};
    EXPECT_TRUE(button.text().isEmpty());
    EXPECT_EQ(button.accessibleName(), "Temporary");
    delete command;
    EXPECT_FALSE(button.isEnabled());
    button.click();
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
