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

#pragma once
#include "component_descriptor.hpp"
#include <QJsonObject>
#include <qt/widgets/shell/action_descriptor.hpp>
#include <string>
#include <variant>
#include <wiola/presentation/documents/document_id.hpp>

namespace wiola::gui {

struct ComponentViewId {
    std::string value;
    auto operator<=>(const ComponentViewId&) const = default;
};
enum class DocumentViewPart {
    whole,
    search,
    tags,
};

struct DocumentViewReference {
    DocumentId document;
    DocumentViewPart part{DocumentViewPart::whole};
    auto operator<=>(const DocumentViewReference&) const = default;
};

using ViewReference = std::variant<ComponentViewId, DocumentViewReference>;

[[nodiscard]] inline ViewReference component_view(std::string key)
{
    return ComponentViewId{std::move(key)};
}

[[nodiscard]] bool valid_view_reference(const ViewReference& reference);
[[nodiscard]] QJsonObject write_view_reference(const ViewReference& reference);
[[nodiscard]] std::optional<ViewReference> read_view_reference(const QJsonObject& object);

struct LegacyViewDescriptor {
    const char* key;
    LayoutComponentKind kind;
    std::optional<PlayerAction> action{};
};

[[nodiscard]] std::span<const LegacyViewDescriptor> legacy_view_descriptors();
/// Only well-known v1 identifiers migrate. A v2 node ID never implicitly borrows a view.
[[nodiscard]] std::optional<ViewReference> legacy_view_reference(const QString& id);

} // namespace wiola::gui
