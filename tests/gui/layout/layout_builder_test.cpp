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

// Legacy fixture adaptation: the tests below keep their ownership assertions, while production
// accepts only explicit references. New identity tests call build directly without this adapter.
bool register_fixture_view(wiola::gui::LayoutBuilder& builder, const char* name, QWidget* widget)
{
    using K = wiola::gui::LayoutComponentKind;
    K kind = K::button;
    if (qobject_cast<QSplitter*>(widget))
        kind = K::splitter;
    else if (qobject_cast<QTabWidget*>(widget))
        kind = K::tabs;
    else if (qobject_cast<QStackedWidget*>(widget))
        kind = K::stack;
    else if (qobject_cast<QGridLayout*>(widget->layout()))
        kind = K::grid;
    else if (qobject_cast<QBoxLayout*>(widget->layout()))
        kind = K::column;
    return builder.views().add(wiola::gui::component_view(name), widget, kind,
        wiola::gui::is_layout_container(kind));
}

QWidget* build_fixture(wiola::gui::LayoutBuilder& builder, wiola::gui::LayoutNode node,
    wiola::gui::LayoutBuildContext context, QWidget* parent = nullptr)
{
    const auto assign = [&](auto&& self, wiola::gui::LayoutNode& current) -> void {
        const auto ref = wiola::gui::component_view(current.id.toStdString());
        if (builder.views().find(ref))
            current.view = ref;
        for (auto& child : current.children)
            self(self, child);
    };
    assign(assign, node);
    return builder.build(node, context, parent);
}

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
    QWidget* widget = build_fixture(builder, root, {.actions = &actions});
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
    QWidget* widget = build_fixture(builder, group, {});
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
    ASSERT_TRUE(register_fixture_view(builder, "play", existing));
    EXPECT_FALSE(register_fixture_view(builder, "play", existing));

    wiola::gui::LayoutNode node;
    node.id = "play";
    node.kind = wiola::gui::LayoutComponentKind::button;

    EXPECT_EQ(build_fixture(builder, node, {}), existing);
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
    ASSERT_TRUE(register_fixture_view(builder, "play", existing));
    QPointer<QPushButton> observed = existing;

    EXPECT_EQ(build_fixture(builder, root, {}), nullptr);
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
    std::unique_ptr<QWidget> widget{build_fixture(builder, document.root, {})};
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
    ASSERT_TRUE(register_fixture_view(builder, "first", first));
    ASSERT_TRUE(register_fixture_view(builder, "second", second));
    wiola::gui::LayoutDocument document;
    ASSERT_EQ(
        wiola::gui::read_layout_json(R"({"version":1,"root":{"id":"grid","type":"grid","columns":2,
        "children":[{"id":"first","type":"button"},{"id":"tabs","type":"tabs","children":[
            {"id":"second","type":"button","title":"Borrowed page"}]}]}})",
            document),
        wiola::gui::LayoutJsonError::none
    );
    QWidget* root = build_fixture(builder, document.root, {}, &owner);
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
    ASSERT_TRUE(register_fixture_view(builder, "tabs", tabs));
    ASSERT_TRUE(register_fixture_view(builder, "first", first));
    ASSERT_TRUE(register_fixture_view(builder, "second", second));
    EXPECT_FALSE(register_fixture_view(builder, "duplicate", second));
    wiola::gui::LayoutDocument document;
    ASSERT_EQ(wiola::gui::read_layout_json(R"({"version":1,"root":{"id":"tabs","type":"tabs",
        "children":[{"id":"second","type":"button","title":"Selected"},
                    {"id":"first","type":"button","title":"Other"}]}})",
                  document),
        wiola::gui::LayoutJsonError::none);

    ASSERT_EQ(build_fixture(builder, document.root, {}, &owner), tabs);
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
    ASSERT_TRUE(register_fixture_view(builder, "borrowed", button));
    // Borrowed widgets must be valid even if a later registered container is incompatible.
    ASSERT_TRUE(register_fixture_view(builder, "grid", new QPushButton{&owner}));
    EXPECT_EQ(build_fixture(builder, document.root, {}), nullptr);
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
        QWidget* root = build_fixture(builder, original.root, {}, &owner);
        ASSERT_NE(root, nullptr);
        QPointer<QWidget> wrapper = root->findChild<QWidget*>("wrapper");
        QPointer<QPushButton> omitted = root->findChild<QPushButton*>("omitted");
        ASSERT_NE(wrapper, nullptr);
        ASSERT_NE(omitted, nullptr);
        ASSERT_TRUE(register_fixture_view(builder, "root", root));
        ASSERT_TRUE(register_fixture_view(builder, "omitted", omitted));

        wiola::gui::LayoutDocument replacement;
        const QByteArray replacement_json = QString{R"({"version":1,"root":{"id":"root","type":"%1",
            "children":[{"id":"replacement","type":"status"}]}})"}
                                                .arg(type)
                                                .toUtf8();
        ASSERT_EQ(wiola::gui::read_layout_json(replacement_json, replacement),
            wiola::gui::LayoutJsonError::none);
        ASSERT_EQ(build_fixture(builder, replacement.root, {}, &owner), root);
        EXPECT_EQ(wrapper, nullptr);
        ASSERT_NE(omitted, nullptr);
        EXPECT_EQ(omitted->parentWidget(), &owner);
        EXPECT_TRUE(omitted->isHidden());
        EXPECT_NE(root->findChild<QWidget*>("replacement"), nullptr);
    }
}

TEST(LayoutBuilder, NodeIdentityDoesNotImplicitlyBorrowAndExplicitViewSurvivesRenaming)
{
    using namespace wiola::gui;
    QWidget owner;
    auto* existing = new QPushButton{"Live", &owner};
    LayoutBuilder builder;
    ASSERT_TRUE(builder.views().add(component_view("playPause"), existing,
        LayoutComponentKind::button));
    LayoutNode fresh{.id = "playPause", .kind = LayoutComponentKind::button};
    auto* created = builder.build(fresh, {}, &owner);
    ASSERT_NE(created, nullptr);
    EXPECT_NE(created, existing);
    delete created;
    fresh.id = "arbitraryNode";
    fresh.view = component_view("playPause");
    EXPECT_EQ(builder.build(fresh, {}, &owner), existing);
}

TEST(LayoutBuilder, RejectsDuplicateUnavailableIncompatibleAndAncestorViewsBeforeMovingAnything)
{
    using namespace wiola::gui;
    QWidget owner;
    auto* button = new QPushButton{"Live", &owner};
    LayoutBuilder builder;
    ASSERT_TRUE(builder.views().add(component_view("live"), button, LayoutComponentKind::button));
    LayoutNode one{.id = "one",
        .kind = LayoutComponentKind::button,
        .view = component_view("live")};
    auto two = one;
    two.id = "two";
    LayoutNode root{
        .id = "root",
        .kind = LayoutComponentKind::column,
        .children = {one, two}
    };
    EXPECT_EQ(builder.build(root, {}, &owner), nullptr);
    EXPECT_EQ(button->parentWidget(), &owner);
    one.kind = LayoutComponentKind::status;
    EXPECT_EQ(builder.build(one, {}, &owner), nullptr);
    one.kind = LayoutComponentKind::button;
    one.view = component_view("absent");
    EXPECT_EQ(builder.build(one, {}, &owner), nullptr);
    ASSERT_TRUE(builder.views().add(component_view("ancestor"), &owner, LayoutComponentKind::column,
        true));
    one.kind = LayoutComponentKind::column;
    one.view = component_view("ancestor");
    EXPECT_EQ(builder.build(one, {}, button), nullptr);
    delete button;
    one.kind = LayoutComponentKind::button;
    one.view = component_view("live");
    EXPECT_EQ(builder.build(one, {}, &owner), nullptr);
}

TEST(LayoutBuilder, OpaqueDocumentHostAndTypedDocumentReferencesKeepTheirKindsAndLifetimes)
{
    using namespace wiola::gui;
    QWidget owner;
    auto* host = new QTabWidget{&owner};
    auto* saved = new QWidget{host};
    host->addTab(saved, "Saved");
    LayoutBuilder builder;
    ASSERT_TRUE(builder.views().add(component_view("documents"), host,
        LayoutComponentKind::document_host));
    const ViewReference reference =
        DocumentViewReference{DocumentId::playlist(wiola::library::PlaylistId{"saved:identity"})};
    ASSERT_TRUE(builder.views().add(reference, saved, LayoutComponentKind::track_table));
    LayoutNode opaque{.id = "hostNode",
        .kind = LayoutComponentKind::document_host,
        .view = component_view("documents")};
    EXPECT_EQ(builder.build(opaque, {}, &owner), host);
    EXPECT_EQ(host->count(), 1);
    opaque.children = {
        {.id = "illegal", .kind = LayoutComponentKind::status}
    };
    EXPECT_EQ(builder.build(opaque, {}, &owner), nullptr);
    EXPECT_EQ(host->widget(0), saved);
    LayoutDocument document{
        .root = {.id = "savedNode", .kind = LayoutComponentKind::track_table, .view = reference},
        .node_count = 1
    };
    LayoutDocument restored;
    ASSERT_EQ(read_layout_json(write_layout_json(document), restored), LayoutJsonError::none);
    EXPECT_EQ(restored.root.view, reference);
    EXPECT_EQ(builder.build(restored.root, {}, &owner), saved);
}

TEST(PreparedLayout, AbandonmentAndDestroyedBorrowedViewsDoNotAttachAnyLiveWidget)
{
    using namespace wiola::gui;
    QWidget owner;
    auto* first = new QPushButton{"First", &owner};
    auto* second = new QPushButton{"Second", &owner};
    auto* old = new QHBoxLayout{&owner};
    old->addWidget(first);
    old->addWidget(second);
    LayoutBuilder builder;
    ASSERT_TRUE(builder.views().add(component_view("first"), first, LayoutComponentKind::button));
    ASSERT_TRUE(builder.views().add(component_view("second"), second, LayoutComponentKind::button));
    LayoutNode root{
        .id = "root",
        .kind = LayoutComponentKind::column,
        .children = {
                     {.id = "one", .kind = LayoutComponentKind::button, .view = component_view("first")},
                     {.id = "two", .kind = LayoutComponentKind::button, .view = component_view("second")}}
    };
    {
        auto candidate = builder.prepare(root, {}, &owner);
        ASSERT_NE(candidate, nullptr);
        EXPECT_TRUE(candidate->ready(&owner));
        EXPECT_EQ(first->parentWidget(), &owner);
        EXPECT_EQ(old->indexOf(first), 0);
    }
    EXPECT_EQ(first->parentWidget(), &owner);
    auto candidate = builder.prepare(root, {}, &owner);
    ASSERT_NE(candidate, nullptr);
    delete second;
    EXPECT_FALSE(candidate->ready(&owner));
    EXPECT_EQ(candidate->attach(&owner), nullptr);
    EXPECT_EQ(first->parentWidget(), &owner);
    EXPECT_EQ(old->indexOf(first), 0);
}

TEST(PreparedLayout, DetectsChangedBorrowedOwnershipAndCanOnlyAttachOnce)
{
    using namespace wiola::gui;
    QWidget owner, other;
    auto* button = new QPushButton{"Live", &owner};
    LayoutBuilder builder;
    ASSERT_TRUE(builder.views().add(component_view("live"), button, LayoutComponentKind::button));
    LayoutNode node{.id = "renamed",
        .kind = LayoutComponentKind::button,
        .view = component_view("live")};
    auto pending = builder.prepare(node, {}, &owner);
    ASSERT_NE(pending, nullptr);
    button->setParent(&other);
    EXPECT_EQ(pending->attach(&owner), nullptr);
    EXPECT_EQ(button->parentWidget(), &other);
    auto accepted = builder.prepare(node, {}, &owner);
    ASSERT_NE(accepted, nullptr);
    EXPECT_EQ(accepted->attach(&owner), button);
    EXPECT_EQ(accepted->attach(&owner), nullptr);
    EXPECT_FALSE(accepted->ready(&owner));
}

TEST(PreparedLayout, RetiringAStructureKeepsOmittedRegisteredCompositeContentsTogether)
{
    using namespace wiola::gui;
    QWidget owner;
    auto* root = new QSplitter{&owner};
    auto* composite = new QTabWidget;
    auto* content = new QWidget;
    composite->addTab(content, "Retained");
    root->addWidget(composite);
    LayoutBuilder builder;
    ASSERT_TRUE(builder.views().add(component_view("root"), root, LayoutComponentKind::splitter,
        true));
    ASSERT_TRUE(builder.views().add(component_view("zparent"), composite,
        LayoutComponentKind::document_host));
    ASSERT_TRUE(builder.views().add(component_view("achild"), content,
        LayoutComponentKind::status));
    LayoutNode node{.id = "newRoot",
        .kind = LayoutComponentKind::splitter,
        .children = {{.id = "replacement", .kind = LayoutComponentKind::status}},
        .view = component_view("root")};
    auto prepared = builder.prepare(node, {}, &owner);
    ASSERT_NE(prepared, nullptr);
    ASSERT_EQ(prepared->attach(&owner), root);
    EXPECT_EQ(composite->parentWidget(), &owner);
    EXPECT_EQ(composite->widget(0), content);
    EXPECT_TRUE(composite->isAncestorOf(content));
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
