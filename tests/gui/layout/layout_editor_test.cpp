/**
 * @file
 * @brief Undoable layout document editing regression tests.
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

#include <gui/layout/layout_editor.hpp>

#include <gtest/gtest.h>

namespace {

wiola::gui::LayoutNode node(const QString& id, wiola::gui::LayoutComponentKind kind)
{
    wiola::gui::LayoutNode result;
    result.id = id;
    result.kind = kind;
    return result;
}

wiola::gui::LayoutDocument document()
{
    wiola::gui::LayoutDocument result;
    result.root = node("root", wiola::gui::LayoutComponentKind::row);
    result.root.children = {
        node("controls", wiola::gui::LayoutComponentKind::group),
        node("play", wiola::gui::LayoutComponentKind::button),
    };
    result.node_count = 3;
    return result;
}

TEST(LayoutEditor, InsertsWithUndoAndRedoWithoutAcceptingDuplicateIds)
{
    wiola::gui::LayoutEditor editor{document()};

    ASSERT_TRUE(editor.insert("root", 1, node("status", wiola::gui::LayoutComponentKind::status)));
    EXPECT_EQ(editor.document().root.children[1].id, "status");
    EXPECT_EQ(editor.document().node_count, 4);
    EXPECT_TRUE(editor.can_undo());
    EXPECT_FALSE(editor.can_redo());

    const auto before_rejection = editor.document();
    EXPECT_FALSE(editor.insert("root", 0, node("status", wiola::gui::LayoutComponentKind::status)));
    EXPECT_EQ(editor.document().root.children.size(), before_rejection.root.children.size());

    ASSERT_TRUE(editor.undo());
    EXPECT_EQ(editor.document().node_count, 3);
    EXPECT_TRUE(editor.can_redo());

    ASSERT_TRUE(editor.redo());
    EXPECT_EQ(editor.document().root.children[1].id, "status");
}

TEST(LayoutEditor, MovesAndConfiguresComponentsWithoutChangingTheirIdentity)
{
    wiola::gui::LayoutEditor editor{document()};

    ASSERT_TRUE(editor.move("play", "controls", 0));
    EXPECT_EQ(editor.document().root.children.size(), 1);
    ASSERT_EQ(editor.document().root.children[0].children.size(), 1);
    EXPECT_EQ(editor.document().root.children[0].children[0].id, "play");

    auto configured = node("play", wiola::gui::LayoutComponentKind::button);
    configured.title = "Start playback";
    configured.width = 72;
    configured.action = wiola::gui::PlayerAction::play;
    ASSERT_TRUE(editor.configure("play", configured));
    EXPECT_EQ(editor.document().root.children[0].children[0].title, "Start playback");
    EXPECT_EQ(editor.document().root.children[0].children[0].width, 72);

    configured.id = "different";
    EXPECT_FALSE(editor.configure("play", configured));
    EXPECT_FALSE(editor.move("root", "controls", 0));
    EXPECT_FALSE(editor.move("controls", "missing", 0));

    ASSERT_TRUE(editor.undo());
    EXPECT_EQ(editor.document().root.children[0].children[0].title, "");
    ASSERT_TRUE(editor.undo());
    ASSERT_EQ(editor.document().root.children.size(), 2);
    EXPECT_EQ(editor.document().root.children[1].id, "play");
}

TEST(LayoutEditor, RemovesSubtreesAndRejectsInvalidConfigurations)
{
    wiola::gui::LayoutEditor editor{document()};
    ASSERT_TRUE(editor.insert("controls", 0,
        node("volume", wiola::gui::LayoutComponentKind::volume)));

    ASSERT_TRUE(editor.remove("controls"));
    EXPECT_EQ(editor.document().node_count, 2);
    EXPECT_EQ(editor.document().root.children[0].id, "play");
    EXPECT_FALSE(editor.remove("root"));

    auto invalid = node("play", wiola::gui::LayoutComponentKind::status);
    invalid.action = wiola::gui::PlayerAction::play;
    EXPECT_FALSE(editor.configure("play", invalid));

    ASSERT_TRUE(editor.undo());
    EXPECT_EQ(editor.document().node_count, 4);
    EXPECT_EQ(editor.document().root.children[0].id, "controls");
    EXPECT_EQ(editor.document().root.children[0].children[0].id, "volume");
}

TEST(LayoutEditor, KeepsSplitterWeightsAlignedThroughEditsAndHistory)
{
    auto layout = document();
    layout.root.kind = wiola::gui::LayoutComponentKind::splitter;
    layout.root.sizes = {2, 5};
    layout.root.orientation = Qt::Vertical;
    wiola::gui::LayoutEditor editor{layout};
    ASSERT_TRUE(editor.insert("root", 1, node("extra", wiola::gui::LayoutComponentKind::status)));
    EXPECT_EQ(editor.document().root.sizes, (std::vector<int>{2, 1, 5}));
    ASSERT_TRUE(editor.move("play", "root", 0));
    EXPECT_EQ(editor.document().root.sizes, (std::vector<int>{5, 2, 1}));
    EXPECT_EQ(editor.document().root.children[0].id, "play");
    ASSERT_TRUE(editor.remove("extra"));
    EXPECT_EQ(editor.document().root.sizes, (std::vector<int>{5, 2}));
    ASSERT_TRUE(editor.undo());
    EXPECT_EQ(editor.document().root.sizes, (std::vector<int>{5, 2, 1}));
    ASSERT_TRUE(editor.redo());
    EXPECT_EQ(editor.document().root.sizes, (std::vector<int>{5, 2}));
    EXPECT_EQ(editor.document().root.orientation, Qt::Vertical);
    EXPECT_EQ(wiola::gui::validate_layout_document(editor.document()),
        wiola::gui::LayoutJsonError::none);
    ASSERT_TRUE(editor.remove("controls"));
    ASSERT_TRUE(editor.move("play", "root", 0));
    EXPECT_EQ(editor.document().root.sizes, (std::vector<int>{5}));
}

TEST(LayoutEditor, ConfiguresContainersAndRejectsMovesIntoLeaves)
{
    wiola::gui::LayoutEditor editor{document()};
    auto grid = node("controls", wiola::gui::LayoutComponentKind::grid);
    grid.columns = 2;
    ASSERT_TRUE(editor.configure("controls", grid));
    ASSERT_TRUE(editor.move("play", "controls", 0));
    EXPECT_EQ(editor.document().root.children[0].columns, 2);
    EXPECT_FALSE(editor.insert("play", 0,
        node("invalid", wiola::gui::LayoutComponentKind::status)));
    EXPECT_FALSE(editor.move("controls", "play", 0));
    ASSERT_TRUE(editor.undo());
    EXPECT_EQ(editor.document().root.children.size(), 2);
    ASSERT_TRUE(editor.undo());
    EXPECT_EQ(editor.document().root.children[0].kind, wiola::gui::LayoutComponentKind::group);
}

} // namespace
