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

#include <gui/player_actions.hpp>

#include <QByteArrayView>
#include <QString>

#include <optional>
#include <vector>

namespace wiola::gui {

enum class LayoutComponentKind {
    row,
    column,
    stack,
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
    LayoutComponentKind kind{LayoutComponentKind::group};
    std::optional<PlayerAction> action;
    std::optional<int> width;
    std::optional<int> height;
    std::optional<int> min_width;
    std::optional<int> min_height;
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

} // namespace wiola::gui
