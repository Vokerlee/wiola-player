/**
 * @file
 * @brief Component registration and layout builder regression tests.
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

#include <gui/layout_builder.hpp>
#include <gui/player_presenter.hpp>
#include <gui/track_table_view.hpp>

#include <QApplication>
#include <QLayout>
#include <QPushButton>
#include <QStackedWidget>

#include <gtest/gtest.h>

namespace {

TEST(LayoutBuilder, BuildsRegisteredComponentsWithDimensionsAndActions)
{
    wiola::gui::LayoutNode play;
    play.id = "play";
    play.kind = wiola::gui::LayoutComponentKind::button;
    play.action = wiola::gui::PlayerAction::play;
    play.width = 64;

    wiola::gui::LayoutNode tracks;
    tracks.id = "tracks";
    tracks.kind = wiola::gui::LayoutComponentKind::track_table;

    wiola::gui::LayoutNode controls;
    controls.id = "controls";
    controls.kind = wiola::gui::LayoutComponentKind::row;
    controls.children = {play, tracks};

    wiola::gui::LayoutNode root;
    root.id = "player";
    root.kind = wiola::gui::LayoutComponentKind::column;
    root.min_width = 640;
    root.children = {controls};

    wiola::gui::PlayerPresenter presenter;
    QWidget styled;
    wiola::gui::PlayerActions actions{styled};
    actions.set_available(wiola::gui::PlayerAction::play, true);

    wiola::gui::LayoutBuilder builder;
    QWidget* widget = builder.build(root, {.presenter = &presenter, .actions = &actions});
    ASSERT_NE(widget, nullptr);

    EXPECT_EQ(widget->minimumWidth(), 640);
    EXPECT_EQ(widget->layout()->count(), 1);

    QPushButton* button = widget->findChild<QPushButton*>("play");
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->width(), 64);
    EXPECT_EQ(button->accessibleName(), "Play");
    EXPECT_TRUE(button->isEnabled());
    EXPECT_NE(widget->findChild<wiola::gui::TrackTableView*>("tracks"), nullptr);

    delete widget;
}

TEST(LayoutBuilder, RejectsDuplicateAndEmptyRegistrations)
{
    wiola::gui::LayoutBuilder builder;

    EXPECT_FALSE(builder.register_component(wiola::gui::LayoutComponentKind::button,
        [](QWidget* parent, const auto&) { return new QPushButton{parent}; }));
    EXPECT_FALSE(builder.register_component(wiola::gui::LayoutComponentKind::status, {}));
}

TEST(LayoutBuilder, BuildsStackedAndGroupedChildren)
{
    wiola::gui::LayoutNode status;
    status.id = "status";
    status.kind = wiola::gui::LayoutComponentKind::status;

    wiola::gui::LayoutNode stack;
    stack.id = "stack";
    stack.kind = wiola::gui::LayoutComponentKind::stack;
    stack.children = {status};

    wiola::gui::LayoutNode group;
    group.id = "group";
    group.kind = wiola::gui::LayoutComponentKind::group;
    group.children = {stack};

    wiola::gui::LayoutBuilder builder;
    QWidget* widget = builder.build(group, {});
    ASSERT_NE(widget, nullptr);

    auto* pages = widget->findChild<QStackedWidget*>("stack");
    ASSERT_NE(pages, nullptr);
    EXPECT_EQ(pages->count(), 1);

    delete widget;
}

TEST(LayoutBuilder, ReusesARegisteredWidgetByIdentity)
{
    auto* existing = new QPushButton{"Existing"};
    wiola::gui::LayoutBuilder builder;
    ASSERT_TRUE(builder.register_widget("play", existing));
    EXPECT_FALSE(builder.register_widget("play", existing));

    wiola::gui::LayoutNode node;
    node.id = "play";
    node.kind = wiola::gui::LayoutComponentKind::button;

    EXPECT_EQ(builder.build(node, {}), existing);
    delete existing;
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
