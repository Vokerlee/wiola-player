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
enum class Section {
    transport,
    library_commands,
    documents,
    queue_commands,
    status,
};

LayoutNode section(Section purpose, PlayerLayoutPreset preset)
{
    using K = LayoutComponentKind;
    using A = PlayerAction;
    switch (purpose) {
    case Section::transport:
        return container("playbackGroup", K::group,
            {leaf("trackLabel", K::status), leaf("positionBar", K::transport),
                container("timeRow", K::row,
                    {leaf("elapsedTime", K::status), leaf("totalDuration", K::status)}),
                container("transportControls", K::row,
                    {leaf("previousTrack", K::button, A::previous),
                        leaf("playPause", K::button, A::play),
                        leaf("nextTrack", K::button, A::next),
                        leaf("stopPlayback", K::button, A::stop),
                        container("volumeControls", K::row,
                            {leaf("volumeSlider", K::volume), leaf("volumeValue", K::status),
                                leaf("boostButton", K::button)},
                            "", "volumeSection")})},
            "Now playing", "transportSection");
    case Section::library_commands:
        return container("libraryCommands", K::row,
            {leaf("openTracks", K::button, A::open), leaf("repeatQueue", K::button, A::repeat),
                leaf("shuffleQueue", K::button, A::shuffle),
                leaf("equalizer", K::button, A::equalizer)});
    case Section::queue_commands:
        return container("queueCommandsGroup", K::group,
            {leaf("addTracks", K::button, A::add), leaf("removeTracks", K::button, A::remove),
                leaf("clearQueue", K::button, A::clear)},
            "Queue commands");
    case Section::status:
        return container("statusArea", K::column,
            {leaf("visibleDocument", K::status), leaf("activeDestination", K::status),
                leaf("playerStatus", K::status)},
            "", "statusSection");
    case Section::documents: {
        auto tabs = leaf("documentTabs", K::document_host);
        if (preset == PlayerLayoutPreset::queue_focus)
            return tabs;
        auto library = container(preset == PlayerLayoutPreset::classic ? "libraryPane"
                                                                       : "libraryFocusNavigation",
            K::column,
            {leaf("libraryNavigation", K::inspector), leaf("savedPlaylistManager", K::inspector)},
            "", preset == PlayerLayoutPreset::classic ? "libraryPane" : nullptr);
        auto body =
            container(preset == PlayerLayoutPreset::classic ? "documentArea" : "libraryFocusBody",
                preset == PlayerLayoutPreset::classic ? K::splitter : K::row,
                {std::move(library), std::move(tabs), leaf("trackInspector", K::inspector)}, "",
                preset == PlayerLayoutPreset::classic ? "documentArea" : nullptr);
        if (preset == PlayerLayoutPreset::classic)
            body.sizes = {180, 620, 220};
        return body;
    }
    }
    return {};
}

} // namespace

LayoutDocument player_layout_document(PlayerLayoutPreset preset)
{
    LayoutDocument document;
    document.root = container(preset == PlayerLayoutPreset::classic ? "classicRoot"
            : preset == PlayerLayoutPreset::library_focus           ? "libraryFocusRoot"
                                                                    : "queueFocusRoot",
        LayoutComponentKind::column, {});
    for (const auto purpose : {Section::transport, Section::library_commands, Section::documents,
             Section::queue_commands, Section::status})
        document.root.children.push_back(section(purpose, preset));
    document.node_count = layout_node_count(document.root);
    return document;
}

} // namespace wiola::gui
