/**
 * @file
 * @brief Bounded versioned JSON description of a player layout.
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

#include "view_reference.hpp"
#include <algorithm>
#include <array>

namespace wiola::gui {

bool valid_view_reference(const ViewReference& reference)
{
    return std::visit(
        [](const auto& value) {
            if constexpr (requires { value.value; })
                return !value.value.empty() && value.value.size() <= 128 &&
                    value.value.find('\0') == std::string::npos;
            else
                return value.document.valid() &&
                    static_cast<unsigned>(value.part) <=
                    static_cast<unsigned>(DocumentViewPart::tags);
        },
        reference
    );
}

QJsonObject write_view_reference(const ViewReference& reference)
{
    if (!valid_view_reference(reference))
        return {};
    if (const auto* component = std::get_if<ComponentViewId>(&reference))
        return {
            {"component", QString::fromStdString(component->value)}
        };
    const auto& view = std::get<DocumentViewReference>(reference);
    QJsonObject document;
    if (const auto* id = view.document.playlist())
        document = {
            {"kind", "playlist"                       },
            {"id",   QString::fromStdString(id->value)}
        };
    else if (const auto* id = view.document.workspace())
        document = {
            {"kind", "workspace"                      },
            {"id",   QString::fromStdString(id->value)}
        };
    else
        document = {
            {"kind", "queue"}
        };
    return {
        {"document", document                                   },
        {"part",
         view.part == DocumentViewPart::whole        ? "whole"
                : view.part == DocumentViewPart::search ? "search"
                                                        : "tags"}
    };
}

std::optional<ViewReference> read_view_reference(const QJsonObject& object)
{
    std::optional<ViewReference> result;
    if (object.size() == 1 && object.value("component").isString())
        result = component_view(object.value("component").toString().toStdString());
    else if (object.size() == 2 && object.value("document").isObject() &&
        object.value("part").isString()) {
        const auto document = object.value("document").toObject();
        const auto kind = document.value("kind").toString();
        const auto id = document.value("id").toString();
        std::optional<DocumentId> identity;
        if (kind == "queue" && document.size() == 1)
            identity = DocumentId::queue();
        else if (document.size() == 2 && !id.isEmpty() && id.toUtf8().size() <= 128) {
            if (kind == "playlist")
                identity = DocumentId::playlist(library::PlaylistId{id.toStdString()});
            else if (kind == "workspace")
                identity = DocumentId::workspace(library::WorkspaceId{id.toStdString()});
        }
        const auto part = object.value("part").toString();
        if (identity && (part == "whole" || part == "search" || part == "tags"))
            result = DocumentViewReference{*identity,
                part == "whole"        ? DocumentViewPart::whole
                    : part == "search" ? DocumentViewPart::search
                                       : DocumentViewPart::tags};
    }
    return result && valid_view_reference(*result) ? result : std::nullopt;
}

std::span<const LegacyViewDescriptor> legacy_view_descriptors()
{
    static constexpr std::array entries{
        LegacyViewDescriptor{"trackLabel", LayoutComponentKind::status},
        LegacyViewDescriptor{"positionBar", LayoutComponentKind::transport},
        LegacyViewDescriptor{"elapsedTime", LayoutComponentKind::status},
        LegacyViewDescriptor{"totalDuration", LayoutComponentKind::status},
        LegacyViewDescriptor{"previousTrack", LayoutComponentKind::button, PlayerAction::previous},
        LegacyViewDescriptor{"playPause", LayoutComponentKind::button, PlayerAction::play},
        LegacyViewDescriptor{"nextTrack", LayoutComponentKind::button, PlayerAction::next},
        LegacyViewDescriptor{"stopPlayback", LayoutComponentKind::button, PlayerAction::stop},
        LegacyViewDescriptor{"volumeSlider", LayoutComponentKind::volume},
        LegacyViewDescriptor{"volumeValue", LayoutComponentKind::status},
        LegacyViewDescriptor{"boostButton", LayoutComponentKind::button},
        LegacyViewDescriptor{"openTracks", LayoutComponentKind::button, PlayerAction::open},
        LegacyViewDescriptor{"repeatQueue", LayoutComponentKind::button, PlayerAction::repeat},
        LegacyViewDescriptor{"shuffleQueue", LayoutComponentKind::button, PlayerAction::shuffle},
        LegacyViewDescriptor{"equalizer", LayoutComponentKind::button, PlayerAction::equalizer},
        LegacyViewDescriptor{"documentArea", LayoutComponentKind::splitter},
        LegacyViewDescriptor{"libraryPane", LayoutComponentKind::column},
        LegacyViewDescriptor{"libraryNavigation", LayoutComponentKind::inspector},
        LegacyViewDescriptor{"savedPlaylistManager", LayoutComponentKind::inspector},
        LegacyViewDescriptor{"documentTabs", LayoutComponentKind::document_host},
        LegacyViewDescriptor{"trackInspector", LayoutComponentKind::inspector},
        LegacyViewDescriptor{"nowPlayingDocument", LayoutComponentKind::track_table},
        LegacyViewDescriptor{"addTracks", LayoutComponentKind::button, PlayerAction::add},
        LegacyViewDescriptor{"removeTracks", LayoutComponentKind::button, PlayerAction::remove},
        LegacyViewDescriptor{"clearQueue", LayoutComponentKind::button, PlayerAction::clear},
        LegacyViewDescriptor{"playerStatus", LayoutComponentKind::status},
        LegacyViewDescriptor{"nowPlayingSearch", LayoutComponentKind::search},
        LegacyViewDescriptor{"nowPlayingFilters", LayoutComponentKind::tags}
    };
    return entries;
}

std::optional<ViewReference> legacy_view_reference(const QString& id)
{
    if (id == "nowPlayingDocument")
        return DocumentViewReference{DocumentId::queue()};
    if (id == "nowPlayingSearch")
        return DocumentViewReference{DocumentId::queue(), DocumentViewPart::search};
    if (id == "nowPlayingFilters")
        return DocumentViewReference{DocumentId::queue(), DocumentViewPart::tags};
    for (const auto& descriptor : legacy_view_descriptors())
        if (id == descriptor.key)
            return component_view(descriptor.key);
    return {};
}

} // namespace wiola::gui
