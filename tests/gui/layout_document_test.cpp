/**
 * @file
 * @brief Bounded layout document parser regression tests.
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

#include <gui/classic_layout.hpp>
#include <gui/layout_document.hpp>

#include <gtest/gtest.h>

#include <QByteArray>

namespace {

using wiola::gui::LayoutJsonError;

TEST(LayoutDocument, ReadsVersionedNestingDimensionsAndActionBindings)
{
    const QByteArray json = R"({
        "version": 1,
        "root": {
            "id": "player",
            "type": "column",
            "size": {"minWidth": 640, "minHeight": 480},
            "children": [
                {"id": "controls", "type": "row", "children": [
                    {"id": "play", "type": "button", "action": "play", "size": {"width": 64}}
                ]},
                {"id": "tracks", "type": "track_table"}
            ]
        }
    })";

    wiola::gui::LayoutDocument document;
    ASSERT_EQ(wiola::gui::read_layout_json(json, document), LayoutJsonError::none);

    EXPECT_EQ(document.version, 1);
    EXPECT_EQ(document.node_count, 4U);
    EXPECT_EQ(document.root.id, "player");
    EXPECT_EQ(document.root.min_width, 640);
    EXPECT_EQ(document.root.min_height, 480);
    ASSERT_EQ(document.root.children.size(), 2U);
    EXPECT_EQ(document.root.children[1].kind, wiola::gui::LayoutComponentKind::track_table);

    const auto& play = document.root.children[0].children[0];
    ASSERT_TRUE(play.action);
    EXPECT_EQ(*play.action, wiola::gui::PlayerAction::play);
    EXPECT_EQ(play.width, 64);
}

TEST(LayoutDocument, RejectsInvalidComponentsWithoutChangingThePreviousDocument)
{
    wiola::gui::LayoutDocument document;
    document.version = 99;

    const QByteArray duplicate = R"({"version":1,"root":{"id":"same","type":"group","children":[
        {"id":"same","type":"button","action":"play"}]}})";
    EXPECT_EQ(wiola::gui::read_layout_json(duplicate, document),
        LayoutJsonError::invalid_component);
    EXPECT_EQ(document.version, 99);

    EXPECT_EQ(wiola::gui::read_layout_json(R"({"version":1,"root":{"id":"Bad ID","type":"row"}})",
                  document),
        LayoutJsonError::invalid_component);
    EXPECT_EQ(wiola::gui::read_layout_json(
                  R"({"version":1,"root":{"id":"button","type":"button","action":"unknown"}})",
                  document
              ),
        LayoutJsonError::invalid_component);
    EXPECT_EQ(wiola::gui::read_layout_json(
                  R"({"version":1,"root":{"id":"row","type":"row","size":{"width":-1}}})", document
              ),
        LayoutJsonError::invalid_component);
}

TEST(LayoutDocument, RejectsMalformedVersionsAndExcessiveInputs)
{
    wiola::gui::LayoutDocument document;

    EXPECT_EQ(wiola::gui::read_layout_json("not json", document), LayoutJsonError::malformed);
    EXPECT_EQ(wiola::gui::read_layout_json(R"({"version":2,"root":{}})", document),
        LayoutJsonError::unsupported_version);
    EXPECT_EQ(wiola::gui::read_layout_json(QByteArray(256 * 1024 + 1, ' '), document),
        LayoutJsonError::too_large);

    QByteArray deep;
    for (int depth = 0; depth < 18; ++depth)
        deep +=
            "{\"id\":\"node" + QByteArray::number(depth) + "\",\"type\":\"group\",\"children\":[";
    deep += "null";
    for (int depth = 0; depth < 18; ++depth)
        deep += "]}";

    const QByteArray nested = QByteArray{"{\"version\":1,\"root\":"} + deep + "}";
    EXPECT_EQ(wiola::gui::read_layout_json(nested, document), LayoutJsonError::limit_exceeded);
}

TEST(LayoutDocument, ClassicPresetIsValidAndComplete)
{
    const auto document = wiola::gui::classic_layout_document();
    EXPECT_GT(document.node_count, 20U);
    EXPECT_EQ(document.root.id, "classicRoot");
}

} // namespace
