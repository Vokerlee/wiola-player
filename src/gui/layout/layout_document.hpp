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

#include <gui/shell/player_actions.hpp>

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <optional>
#include <vector>

namespace wiola::gui {

enum class LayoutComponentKind {
    row,
    column,
    stack,
    splitter,
    grid,
    tabs,
    group,
    button,
    track_table,
    inspector,
    search,
    tags,
    transport,
    volume,
    status,
};

struct LayoutNode {
    QString id;
    QString title;
    LayoutComponentKind kind{LayoutComponentKind::group};
    std::optional<PlayerAction> action;
    std::optional<int> width;
    std::optional<int> height;
    std::optional<int> min_width;
    std::optional<int> min_height;
    /// Splitter orientation; absent means horizontal. Sizes are positive relative weights.
    std::optional<Qt::Orientation> orientation;
    std::vector<int> sizes;
    /// Grid children fill rows in document order; absent means one column.
    std::optional<int> columns;
    std::vector<LayoutNode> children;
};

struct LayoutDocument {
    int version{1};
    LayoutNode root;
    std::size_t node_count{0};
};

enum class LayoutJsonError {
    none,
    too_large,
    malformed,
    unsupported_version,
    invalid_component,
    limit_exceeded,
};

[[nodiscard]] LayoutJsonError read_layout_json(QByteArrayView json, LayoutDocument& output);
[[nodiscard]] LayoutJsonError validate_layout_document(const LayoutDocument& document);
[[nodiscard]] bool is_layout_container(LayoutComponentKind kind) noexcept;
[[nodiscard]] QString layout_component_name(LayoutComponentKind kind);
/// Returns an empty byte array for an invalid document.
[[nodiscard]] QByteArray write_layout_json(const LayoutDocument& document);

} // namespace wiola::gui
