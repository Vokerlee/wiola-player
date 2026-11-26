/**
 * @file
 * @brief A length of time, written the way a listener reads it.
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

#include "player_layout.hpp"
#include "layout_tree.hpp"

namespace wiola::gui {
namespace {

LayoutNode container(const char* id, LayoutComponentKind kind, std::vector<LayoutNode> children,
    const char* title = "", const char* view = nullptr)
{
    LayoutNode node;
    node.id = id;
    node.kind = kind;
    node.title = title;
    node.children = std::move(children);
    if (view)
        node.view = component_view(view);
    return node;
}

LayoutNode leaf(const char* id, LayoutComponentKind kind, std::optional<PlayerAction> action = {})
{
    auto node = container(id, kind, {}, "", id);
    node.action = action;
    return node;
}

LayoutNode transport()
{
    using K = LayoutComponentKind;
    using A = PlayerAction;
    auto controls = container("transportControls", K::row,
        {leaf("previousTrack", K::button, A::previous), leaf("playPause", K::button, A::play),
            leaf("nextTrack", K::button, A::next), leaf("stopPlayback", K::button, A::stop)});
    controls.alignment = Qt::AlignHCenter;
    auto options = container("playbackOptions", K::row,
        {leaf("repeatQueue", K::button, A::repeat), leaf("shuffleQueue", K::button, A::shuffle),
            leaf("equalizer", K::button, A::equalizer)});
    options.alignment = Qt::AlignRight;
    auto volume = container("volumeControls", K::row,
        {leaf("volumeSlider", K::volume), leaf("volumeValue", K::status),
            leaf("boostButton", K::button)},
        "", "volumeSection");
    volume.alignment = Qt::AlignRight;
    auto regions = container("playbackRegions", K::grid,
        {leaf("trackLabel", K::status),
            container("playbackCenter", K::column,
                {std::move(controls),
                    container("timeline", K::column,
                        {leaf("positionBar", K::transport),
                            container("timeRow", K::row,
                                {leaf("elapsedTime", K::status),
                                    leaf("totalDuration", K::status)})})}),
            container("playbackUtilities", K::column, {std::move(options), std::move(volume)})});
    regions.columns = 3;
    return container("playbackGroup", K::group, {std::move(regions)}, "", "transportSection");
}

} // namespace

LayoutDocument player_layout_document(PlayerLayoutPreset preset)
{
    using K = LayoutComponentKind;
    using A = PlayerAction;
    LayoutDocument document;
    document.root = container(preset == PlayerLayoutPreset::classic ? "classicRoot"
                                                                    : "libraryFocusRoot",
        K::column,
        {container("libraryCommands", K::row,
             {leaf("openTracks", K::button, A::open), leaf("addTracks", K::button, A::add),
                 leaf("removeTracks", K::button, A::remove),
                 leaf("clearQueue", K::button, A::clear)}),
            leaf("documentTabs", K::document_host), transport(), leaf("playerStatus", K::status)});
    document.node_count = layout_node_count(document.root);
    return document;
}

} // namespace wiola::gui
