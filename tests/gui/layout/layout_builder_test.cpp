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

#include <gui/layout/layout_builder.hpp>
#include <gui/tracks/track_table_view.hpp>

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLayout>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>

#include <gtest/gtest.h>

#include <memory>

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

    QWidget styled;
    wiola::gui::PlayerActions actions{styled};
    actions.set_available(wiola::gui::PlayerAction::play, true);

    wiola::gui::LayoutBuilder builder;
    QWidget* widget = builder.build(root, {.actions = &actions});
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

TEST(LayoutBuilder, FailedConstructionLeavesRegisteredWidgetsInTheirOriginalLayout)
{
    QWidget original;
    auto* existing = new QPushButton{"Existing", &original};
    auto* trailing = new QPushButton{"Trailing", &original};
    auto* layout = new QHBoxLayout{&original};
    layout->addWidget(existing);
    layout->addWidget(trailing);

    wiola::gui::LayoutNode reused;
    reused.id = "play";
    reused.kind = wiola::gui::LayoutComponentKind::button;

    wiola::gui::LayoutNode invalid;
    invalid.id = "invalid";
    invalid.kind = static_cast<wiola::gui::LayoutComponentKind>(999);

    wiola::gui::LayoutNode root;
    root.id = "root";
    root.kind = wiola::gui::LayoutComponentKind::row;
    root.children = {reused, invalid};

    wiola::gui::LayoutBuilder builder;
    ASSERT_TRUE(builder.register_widget("play", existing));
    QPointer<QPushButton> observed = existing;

    EXPECT_EQ(builder.build(root, {}), nullptr);
    EXPECT_EQ(observed, existing);
    EXPECT_EQ(existing->parentWidget(), &original);
    EXPECT_EQ(layout->indexOf(existing), 0);
    EXPECT_EQ(layout->indexOf(trailing), 1);
}

TEST(LayoutBuilder, BuildsVerticalSplitterWithGridAndLabeledTabs)
{
    wiola::gui::LayoutDocument document;
    ASSERT_EQ(wiola::gui::read_layout_json(R"({"version":1,"root":{"id":"panes",
        "type":"splitter","orientation":"vertical","sizes":[1,3],"children":[
        {"id":"grid","type":"grid","columns":2,"children":[
            {"id":"one","type":"status"},{"id":"two","type":"status"},{"id":"three","type":"status"}]},
        {"id":"tabs","type":"tabs","children":[
            {"id":"first","type":"status","title":"First"},{"id":"second","type":"status"}]}]}})",
                  document),
        wiola::gui::LayoutJsonError::none);
    wiola::gui::LayoutBuilder builder;
    std::unique_ptr<QWidget> widget{builder.build(document.root, {})};
    auto* splitter = qobject_cast<QSplitter*>(widget.get());
    ASSERT_NE(splitter, nullptr);
    EXPECT_EQ(splitter->orientation(), Qt::Vertical);
    EXPECT_FALSE(splitter->childrenCollapsible());
    ASSERT_EQ(splitter->count(), 2);
    auto* grid = qobject_cast<QGridLayout*>(splitter->widget(0)->layout());
    ASSERT_NE(grid, nullptr);
    EXPECT_EQ(grid->itemAtPosition(1, 0)->widget()->objectName(), "three");
    auto* tabs = qobject_cast<QTabWidget*>(splitter->widget(1));
    ASSERT_NE(tabs, nullptr);
    EXPECT_EQ(tabs->count(), 2);
    EXPECT_EQ(tabs->tabText(0), "First");
    EXPECT_EQ(tabs->tabText(1), "second");
}

TEST(LayoutBuilder, PlacesBorrowedWidgetsInGridCellsAndTabPages)
{
    QWidget owner;
    auto* first = new QPushButton{"Borrowed first", &owner};
    auto* second = new QPushButton{"Borrowed second", &owner};
    wiola::gui::LayoutBuilder builder;
    ASSERT_TRUE(builder.register_widget("first", first));
    ASSERT_TRUE(builder.register_widget("second", second));
    wiola::gui::LayoutDocument document;
    ASSERT_EQ(
        wiola::gui::read_layout_json(R"({"version":1,"root":{"id":"grid","type":"grid","columns":2,
        "children":[{"id":"first","type":"button"},{"id":"tabs","type":"tabs","children":[
            {"id":"second","type":"button","title":"Borrowed page"}]}]}})",
            document),
        wiola::gui::LayoutJsonError::none
    );
    QWidget* root = builder.build(document.root, {}, &owner);
    ASSERT_NE(root, nullptr);
    auto* grid = qobject_cast<QGridLayout*>(root->layout());
    ASSERT_NE(grid, nullptr);
    EXPECT_EQ(grid->itemAtPosition(0, 0)->widget(), first);
    auto* tabs = qobject_cast<QTabWidget*>(grid->itemAtPosition(0, 1)->widget());
    ASSERT_NE(tabs, nullptr);
    EXPECT_EQ(tabs->widget(0), second);
    EXPECT_EQ(tabs->tabText(0), "Borrowed page");
}

TEST(LayoutBuilder, ReordersBorrowedTabPagesAndPreservesTheSelectedPage)
{
    QWidget owner;
    auto* tabs = new QTabWidget{&owner};
    auto* first = new QPushButton{"First"};
    auto* second = new QPushButton{"Second"};
    tabs->addTab(first, "Old first");
    tabs->addTab(second, "Old second");
    tabs->setCurrentWidget(second);
    wiola::gui::LayoutBuilder builder;
    ASSERT_TRUE(builder.register_widget("tabs", tabs));
    ASSERT_TRUE(builder.register_widget("first", first));
    ASSERT_TRUE(builder.register_widget("second", second));
    EXPECT_FALSE(builder.register_widget("duplicate", second));
    wiola::gui::LayoutDocument document;
    ASSERT_EQ(wiola::gui::read_layout_json(R"({"version":1,"root":{"id":"tabs","type":"tabs",
        "children":[{"id":"second","type":"button","title":"Selected"},
                    {"id":"first","type":"button","title":"Other"}]}})",
                  document),
        wiola::gui::LayoutJsonError::none);

    ASSERT_EQ(builder.build(document.root, {}, &owner), tabs);
    ASSERT_EQ(tabs->count(), 2);
    EXPECT_EQ(tabs->widget(0), second);
    EXPECT_EQ(tabs->widget(1), first);
    EXPECT_EQ(tabs->currentWidget(), second);
    EXPECT_EQ(tabs->tabText(0), "Selected");
    EXPECT_EQ(tabs->tabText(1), "Other");
}

TEST(LayoutBuilder, IncompatibleNestedContainerDoesNotMoveBorrowedWidgets)
{
    QWidget owner;
    auto* button = new QPushButton{"Keep me", &owner};
    auto* original = new QHBoxLayout{&owner};
    original->addWidget(button);
    wiola::gui::LayoutDocument document;
    ASSERT_EQ(
        wiola::gui::read_layout_json(R"({"version":1,"root":{"id":"tabs","type":"tabs","children":[
        {"id":"borrowed","type":"button"},{"id":"grid","type":"grid","children":[
            {"id":"failure","type":"status"}]}]}})",
            document),
        wiola::gui::LayoutJsonError::none
    );
    wiola::gui::LayoutBuilder builder;
    ASSERT_TRUE(builder.register_widget("borrowed", button));
    // Borrowed widgets must be valid even if a later registered container is incompatible.
    ASSERT_TRUE(builder.register_widget("grid", new QPushButton{&owner}));
    EXPECT_EQ(builder.build(document.root, {}), nullptr);
    EXPECT_EQ(button->parentWidget(), &owner);
    EXPECT_EQ(original->indexOf(button), 0);
}

TEST(LayoutBuilder, RetiresDiscardedContainersWithoutDeletingOmittedRegisteredViews)
{
    for (const QString type : {"splitter", "tabs", "stack", "grid", "column"}) {
        SCOPED_TRACE(type.toStdString());
        QWidget owner;
        wiola::gui::LayoutBuilder builder;
        wiola::gui::LayoutDocument original;
        const QByteArray original_json = QString{R"({"version":1,"root":{"id":"root","type":"%1",
            "children":[{"id":"wrapper","type":"column","children":[
                {"id":"omitted","type":"button"}]}]}})"}
                                             .arg(type)
                                             .toUtf8();
        ASSERT_EQ(wiola::gui::read_layout_json(original_json, original),
            wiola::gui::LayoutJsonError::none);
        QWidget* root = builder.build(original.root, {}, &owner);
        ASSERT_NE(root, nullptr);
        QPointer<QWidget> wrapper = root->findChild<QWidget*>("wrapper");
        QPointer<QPushButton> omitted = root->findChild<QPushButton*>("omitted");
        ASSERT_NE(wrapper, nullptr);
        ASSERT_NE(omitted, nullptr);
        ASSERT_TRUE(builder.register_widget("root", root));
        ASSERT_TRUE(builder.register_widget("omitted", omitted));

        wiola::gui::LayoutDocument replacement;
        const QByteArray replacement_json = QString{R"({"version":1,"root":{"id":"root","type":"%1",
            "children":[{"id":"replacement","type":"status"}]}})"}
                                                .arg(type)
                                                .toUtf8();
        ASSERT_EQ(wiola::gui::read_layout_json(replacement_json, replacement),
            wiola::gui::LayoutJsonError::none);
        ASSERT_EQ(builder.build(replacement.root, {}, &owner), root);
        EXPECT_EQ(wrapper, nullptr);
        ASSERT_NE(omitted, nullptr);
        EXPECT_EQ(omitted->parentWidget(), &owner);
        EXPECT_TRUE(omitted->isHidden());
        EXPECT_NE(root->findChild<QWidget*>("replacement"), nullptr);
    }
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
