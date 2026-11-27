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

#include <qt/widgets/layout/classic_layout.hpp>
#include <qt/widgets/layout/focused_layout.hpp>
#include <qt/widgets/layout/layout_document.hpp>

#include <QByteArray>
#include <gtest/gtest.h>

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

    EXPECT_EQ(document.version, 2);
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
    EXPECT_EQ(wiola::gui::read_layout_json(R"({"version":3,"root":{}})", document),
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

TEST(LayoutDocument, RowAlignmentRoundTripsAndRejectsInvalidValuesTransactionally)
{
    using namespace wiola::gui;

    for (const auto alignment : {Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight}) {
        LayoutDocument document{
            .root = {.id = "row", .kind = LayoutComponentKind::row},
            .node_count = 1
        };
        document.root.alignment = alignment;

        LayoutDocument restored;

        ASSERT_EQ(read_layout_json(write_layout_json(document), restored), LayoutJsonError::none);
        EXPECT_EQ(restored.root.alignment, alignment);
        EXPECT_EQ(parse_layout_alignment(layout_alignment_name(alignment)), alignment);
    }

    EXPECT_TRUE(layout_alignment_name(Qt::AlignTop).isEmpty());
    EXPECT_FALSE(parse_layout_alignment("bottom"));

    LayoutDocument untouched = wiola::gui::classic_layout_document();

    for (const QByteArray json :
        {R"({"version":2,"root":{"id":"bad","type":"row","alignment":true}})",
            R"({"version":2,"root":{"id":"bad","type":"row","alignment":"bottom"}})",
            R"({"version":2,"root":{"id":"bad","type":"column","alignment":"center"}})"}) {
        EXPECT_EQ(read_layout_json(json, untouched), LayoutJsonError::invalid_component);
        EXPECT_EQ(untouched.root.id, "classicRoot");
    }

    untouched.root.alignment = Qt::AlignTop;
    EXPECT_EQ(validate_layout_document(untouched), LayoutJsonError::invalid_component);
}

TEST(LayoutDocument, ClassicPresetIsValidAndComplete)
{
    const auto document = wiola::gui::classic_layout_document();

    EXPECT_GT(document.node_count, 20U);
    EXPECT_EQ(document.root.id, "classicRoot");
}

TEST(LayoutDocument, LibraryPresetRetainsCompletePlayerControls)
{
    const auto library = wiola::gui::library_focus_layout_document();
    const auto classic = wiola::gui::classic_layout_document();

    EXPECT_EQ(library.root.id, "libraryFocusRoot");
    EXPECT_EQ(library.node_count, classic.node_count);
    EXPECT_EQ(library.root.children.front().id, classic.root.children.front().id);
    EXPECT_GT(library.node_count, 20U);
}

TEST(LayoutDocument, RoundTripsNestedContainersWithTheirProperties)
{
    const QByteArray json = R"({"version":1,"root":{"id":"panes","type":"splitter",
        "orientation":"vertical","sizes":[2,3],"children":[
        {"id":"grid","type":"grid","columns":2,"children":[
            {"id":"play","type":"button","action":"play","size":{"width":64}},
            {"id":"status","type":"status"}]},
        {"id":"tabs","type":"tabs","children":[
            {"id":"first","type":"status","title":"First page"},
            {"id":"second","type":"status"}]}]}})";
    wiola::gui::LayoutDocument document;

    ASSERT_EQ(wiola::gui::read_layout_json(json, document), LayoutJsonError::none);
    ASSERT_EQ(wiola::gui::validate_layout_document(document), LayoutJsonError::none);
    EXPECT_EQ(document.root.orientation, Qt::Vertical);
    EXPECT_EQ(document.root.sizes, (std::vector<int>{2, 3}));
    EXPECT_EQ(document.root.children[0].columns, 2);

    const QByteArray serialized = wiola::gui::write_layout_json(document);

    ASSERT_FALSE(serialized.isEmpty());

    wiola::gui::LayoutDocument restored;

    ASSERT_EQ(wiola::gui::read_layout_json(serialized, restored), LayoutJsonError::none);
    EXPECT_EQ(wiola::gui::write_layout_json(restored), serialized);
}

TEST(LayoutDocument, RejectsInvalidContainerPropertiesTransactionally)
{
    wiola::gui::LayoutDocument document = wiola::gui::classic_layout_document();

    for (const QByteArray json :
        {R"({"version":1,"root":{"id":"x","type":"splitter","orientation":"diagonal"}})",
            R"({"version":1,"root":{"id":"x","type":"splitter","sizes":[0]}})",
            R"({"version":1,"root":{"id":"x","type":"splitter","sizes":[1.5]}})",
            R"({"version":1,"root":{"id":"x","type":"splitter","sizes":[1]}})",
            R"({"version":1,"root":{"id":"x","type":"grid","columns":0}})",
            R"({"version":1,"root":{"id":"x","type":"grid","columns":33}})",
            R"({"version":1,"root":{"id":"x","type":"row","columns":2}})",
            R"({"version":1,"root":{"id":"x","type":"tabs","sizes":[]}})",
            R"({"version":1,"root":{"id":"x","type":"button","children":[{"id":"y","type":"status"}]}})"}) {
        EXPECT_EQ(wiola::gui::read_layout_json(json, document), LayoutJsonError::invalid_component)
            << json.constData();
        EXPECT_EQ(document.root.id, "classicRoot");
    }

    document.root.columns = 2;
    EXPECT_EQ(wiola::gui::validate_layout_document(document), LayoutJsonError::invalid_component);
    EXPECT_TRUE(wiola::gui::write_layout_json(document).isEmpty());
}

TEST(LayoutDocument, DescriptorsRoundTripEveryStableComponentAndActionKey)
{
    using namespace wiola::gui;

    for (const auto& component : component_descriptors()) {
        LayoutDocument document{
            .root = {.id = "root", .kind = component.kind},
            .node_count = 1
        };
        auto encoded = write_layout_json(document);

        ASSERT_FALSE(encoded.isEmpty());

        LayoutDocument decoded;

        ASSERT_EQ(read_layout_json(encoded, decoded), LayoutJsonError::none);
        EXPECT_EQ(decoded.root.kind, component.kind);
        EXPECT_EQ(parse_component(component.key), component.kind);
    }

    for (const auto& action : action_descriptors()) {
        LayoutDocument document{
            .root = {.id = "button", .kind = LayoutComponentKind::button, .action = action.id},
            .node_count = 1
        };
        const auto encoded = write_layout_json(document);

        ASSERT_FALSE(encoded.isEmpty());

        LayoutDocument decoded;

        ASSERT_EQ(read_layout_json(encoded, decoded), LayoutJsonError::none);
        EXPECT_EQ(decoded.root.action, action.id);
        EXPECT_EQ(parse_action(action.key), action.id);
    }

    EXPECT_FALSE(parse_component("QWidget"));
    EXPECT_FALSE(parse_action("execute"));
    EXPECT_EQ(action_descriptors().size(), static_cast<std::size_t>(PlayerAction::count));
}

TEST(LayoutDocument, MigratesOnlyWellKnownVersionOneViewsToVersionTwoReferences)
{
    using namespace wiola::gui;

    LayoutDocument document;

    ASSERT_EQ(read_layout_json(R"({"version":1,"root":{"id":"documentTabs","type":"stack"}})",
                  document),
        LayoutJsonError::none);
    EXPECT_EQ(document.version, 2);
    EXPECT_EQ(document.root.kind, LayoutComponentKind::document_host);
    ASSERT_TRUE(document.root.view);
    EXPECT_EQ(*document.root.view, component_view("documentTabs"));
    ASSERT_EQ(read_layout_json(R"({"version":2,"root":{"id":"documentTabs","type":"stack"}})",
                  document),
        LayoutJsonError::none);
    EXPECT_FALSE(document.root.view);
    EXPECT_EQ(document.root.kind, LayoutComponentKind::stack);

    const auto before = write_layout_json(document);

    EXPECT_EQ(
        read_layout_json(
            R"({"version":2,"root":{"id":"view","type":"status","view":{"component":"x","document":{}}}})",
            document
        ),
        LayoutJsonError::invalid_component
    );
    EXPECT_EQ(write_layout_json(document), before);
}

} // namespace
