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

#include <QApplication>

#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <gtest/gtest.h>
#include <qt/widgets/controls/seek_bar.hpp>
#include <qt/widgets/controls/status_view.hpp>
#include <qt/widgets/controls/transport_view.hpp>
#include <qt/widgets/controls/volume_view.hpp>
#include <qt/widgets/layout/layout_tree.hpp>
#include <qt/widgets/layout/player_layout.hpp>
#include <qt/widgets/layout/view_registry.hpp>
#include <wiola/media/local/resource.hpp>

namespace {

using namespace wiola::gui;

QWidget* view(const ViewRegistry& registry, const char* key)
{
    const auto* found = registry.find(component_view(key));
    return found ? found->widget.data() : nullptr;
}

TEST(PlayerSections, VolumeProjectionNeverIssuesCommandsAndInputsIssueExactlyOne)
{
    VolumeView volume;
    ViewRegistry views;
    volume.register_views(views);
    int commands = 0, boosts = 0, asked = -1;
    QObject::connect(&volume, &VolumeView::volume_requested, [&](int position) {
        ++commands;
        asked = position;
    });
    QObject::connect(&volume, &VolumeView::boost_requested, [&](bool) { ++boosts; });
    volume.present({180, 300, true});
    volume.present({50, 100, false});
    EXPECT_EQ(commands, 0);
    EXPECT_EQ(boosts, 0);
    auto* slider = qobject_cast<QSlider*>(view(views, "volumeSlider"));
    ASSERT_NE(slider, nullptr);
    EXPECT_EQ(slider->value(), 50);
    EXPECT_EQ(slider->maximum(), 100);
    slider->setValue(75);
    EXPECT_EQ(commands, 1);
    EXPECT_EQ(asked, 75);
    qobject_cast<QPushButton*>(view(views, "boostButton"))->click();
    EXPECT_EQ(boosts, 1);
}

TEST(PlayerSections, TransportProjectsUnknownDurationAndKeepsBindingsWhenReparented)
{
    QWidget owner;
    PlayerActions actions{owner};
    TransportView transport{actions};
    ViewRegistry views;
    transport.register_views(views);
    wiola::app::PlayerPresentation state;
    transport.present(state);
    auto* seek = qobject_cast<SeekBar*>(view(views, "positionBar"));
    ASSERT_NE(seek, nullptr);
    EXPECT_FALSE(seek->isEnabled());
    EXPECT_FALSE(actions.action(PlayerAction::play)->isEnabled());
    state.playback.loaded = true;
    state.playback.playing = true;
    state.playback.seekable = true;
    state.playback.track = wiola::media::local_resource("<not-rich-text>.wav");
    state.playback.time_played = wiola::units::Time{30};
    transport.present(state);
    EXPECT_FALSE(seek->isEnabled());
    auto* title = qobject_cast<QLabel*>(view(views, "trackLabel"));
    EXPECT_EQ(title->textFormat(), Qt::PlainText);
    EXPECT_EQ(title->text(), "<not-rich-text>.wav");
    EXPECT_EQ(qobject_cast<QLabel*>(view(views, "totalDuration"))->text(), "--:--");
    state.playback.total_time = wiola::units::Time{120};
    transport.present(state);
    EXPECT_TRUE(seek->isEnabled());
    EXPECT_DOUBLE_EQ(seek->fraction(), 0.25);
    auto* play = qobject_cast<QPushButton*>(view(views, "playPause"));
    QWidget custom;
    play->setParent(&custom);
    int calls = 0;
    QObject::connect(actions.action(PlayerAction::play), &QAction::triggered, [&] { ++calls; });
    play->click();
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(play->accessibleName(), "Pause");
    state.playback.playing = false;
    transport.present(state);
    EXPECT_EQ(play->accessibleName(), "Play");
    play->setParent(&transport);
    state.playback.loaded = false;
    transport.present(state);
    EXPECT_FALSE(play->isEnabled());
    EXPECT_DOUBLE_EQ(seek->fraction(), 0);
}

TEST(PlayerSections, ContextAndErrorsRemainPlainAccessibleText)
{
    StatusView status;
    ViewRegistry views;
    status.register_views(views);
    status.show_context("<b>Road</b>", "Morning");
    status.show_message("Could not save; retry");
    auto* label = qobject_cast<QLabel*>(view(views, "visibleDocument"));
    EXPECT_EQ(label->textFormat(), Qt::PlainText);
    EXPECT_EQ(label->text(), "Viewing: <b>Road</b>");
    auto* message = qobject_cast<QLabel*>(view(views, "playerStatus"));
    EXPECT_EQ(message->accessibleDescription(), message->text());
    EXPECT_EQ(qobject_cast<QLabel*>(view(views, "activeDestination"))->text(),
        "Transfer destination: Morning");
}

TEST(PlayerSections, PresetsShareTypedSectionReferencesAndValidateAfterRoundTrip)
{
    for (const auto preset : {PlayerLayoutPreset::classic, PlayerLayoutPreset::library_focus,
             PlayerLayoutPreset::queue_focus}) {
        const auto document = player_layout_document(preset);
        EXPECT_EQ(validate_layout_document(document), LayoutJsonError::none);
        EXPECT_EQ(document.node_count, layout_node_count(document.root));
        for (const auto& [id, key] : {
                 std::pair{"playbackGroup",  "transportSection"},
                 std::pair{"volumeControls", "volumeSection"   },
                 std::pair{"statusArea",     "statusSection"   }
        }) {
            const auto found = locate_layout_node(document.root, id);
            ASSERT_NE(found.node, nullptr);
            EXPECT_EQ(found.node->view, component_view(key));
        }
        LayoutDocument restored;
        ASSERT_EQ(read_layout_json(write_layout_json(document), restored), LayoutJsonError::none);
        EXPECT_EQ(write_layout_json(restored), write_layout_json(document));
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "minimal");
    QApplication app{argc, argv};
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
