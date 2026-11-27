#include <qt/widgets/layout/layout_builder.hpp>
#include <qt/widgets/layout/layout_editor.hpp>
#include <qt/widgets/tests/support/gui_interaction.hpp>

#include <QGridLayout>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>

#include <memory>

namespace {

using namespace wiola::gui;

class LayoutComponentContract : public ::testing::TestWithParam<LayoutComponentKind> { };

TEST_P(LayoutComponentContract, NestedAttachmentIsSingleUseAndPreservesBorrowedOwnership)
{
    const auto kind = GetParam();
    QWidget owner;
    owner.setUpdatesEnabled(false);

    auto* borrowed = new QPushButton{"Borrowed", &owner};
    QPointer<QWidget> witness{borrowed};
    PlayerActions actions{owner};
    actions.set_available(PlayerAction::play, true);

    LayoutBuilder builder;
    const auto reference = component_view("borrowed");

    ASSERT_TRUE(builder.views().add(reference, borrowed, LayoutComponentKind::button));

    LayoutNode leaf;
    leaf.id = "borrowed-view";
    leaf.kind = LayoutComponentKind::button;
    leaf.view = reference;

    LayoutNode action;
    action.id = "generated-play";
    action.kind = LayoutComponentKind::button;
    action.action = PlayerAction::play;

    LayoutNode root;
    root.id = "nested-root";
    root.kind = kind;
    root.title = "Nested";
    root.children = {leaf, action};

    if (kind == LayoutComponentKind::splitter) {
        root.orientation = Qt::Vertical;
        root.sizes = {1, 2};
    }

    if (kind == LayoutComponentKind::grid)
        root.columns = 2;

    auto prepared = builder.prepare(root, {.actions = &actions}, &owner);

    ASSERT_NE(prepared, nullptr);
    EXPECT_TRUE(prepared->ready(&owner));
    EXPECT_EQ(borrowed->parentWidget(), &owner);

    std::unique_ptr<QWidget> attached{prepared->attach(&owner)};

    ASSERT_NE(attached, nullptr);
    EXPECT_TRUE(attached->isAncestorOf(borrowed));
    EXPECT_FALSE(prepared->ready(&owner));
    EXPECT_EQ(prepared->attach(&owner), nullptr);

    auto* button = attached->findChild<QPushButton*>("generated-play");

    ASSERT_NE(button, nullptr);

    int commands = 0;
    QObject::connect(actions.action(PlayerAction::play), &QAction::triggered, [&] { ++commands; });

    QTest::mouseClick(button, Qt::LeftButton);
    EXPECT_EQ(commands, 1);

    actions.set_available(PlayerAction::play, false);
    EXPECT_FALSE(button->isEnabled());

    QTest::mouseClick(button, Qt::LeftButton);
    EXPECT_EQ(commands, 1);

    // Preparing another tree leaves the currently attached borrowed view untouched.
    auto second = builder.prepare(root, {.actions = &actions}, &owner);

    ASSERT_NE(second, nullptr);

    auto* current_parent = borrowed->parentWidget();

    second.reset();
    EXPECT_EQ(borrowed->parentWidget(), current_parent);

    auto duplicate = root;
    duplicate.children.push_back(leaf);
    duplicate.children.back().id = "duplicate-reference";
    EXPECT_EQ(builder.prepare(duplicate, {.actions = &actions}, &owner), nullptr);
    EXPECT_EQ(borrowed->parentWidget(), current_parent);

    LayoutBuildContext preview{.actions = nullptr, .isolated_preview = true};
    std::unique_ptr<QWidget> isolated{builder.build(root, preview, &owner)};

    ASSERT_NE(isolated, nullptr);
    EXPECT_FALSE(isolated->isAncestorOf(borrowed));

    auto* preview_button = isolated->findChild<QPushButton*>("generated-play");

    ASSERT_NE(preview_button, nullptr);

    QTest::mouseClick(preview_button, Qt::LeftButton);
    EXPECT_EQ(commands, 1);

    borrowed->setParent(&owner);
    attached.reset();
    isolated.reset();
    EXPECT_FALSE(witness.isNull());
    EXPECT_EQ(borrowed->parentWidget(), &owner);
}

INSTANTIATE_TEST_SUITE_P(AllContainers, LayoutComponentContract,
    ::testing::Values(LayoutComponentKind::row, LayoutComponentKind::column,
        LayoutComponentKind::group, LayoutComponentKind::grid, LayoutComponentKind::splitter,
        LayoutComponentKind::tabs, LayoutComponentKind::stack));

} // namespace

int main(int argc, char** argv)
{
    return wiola::testing::run_gui_tests(argc, argv);
}
