/**
 * @file
 * @brief Registered widget factories and recursive layout construction.
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

#include "layout_document.hpp"
#include "view_registry.hpp"

#include <functional>
#include <map>

class QWidget;

namespace wiola::library {

class InMemoryRepository;

}

namespace wiola::gui {

class PlayerActions;

struct LayoutBuildContext {
    PlayerActions* actions{nullptr};
    library::InMemoryRepository* library{nullptr};
    bool isolated_preview{false};
};

class LayoutBuilder final {
public:
    using Factory = std::function<QWidget*(QWidget* parent, const LayoutBuildContext& context)>;

    LayoutBuilder();

    explicit LayoutBuilder(ViewRegistry views)
        : LayoutBuilder{}
    {
        views_ = std::move(views);
    }

    [[nodiscard]] ViewRegistry& views() { return views_; }

    [[nodiscard]] bool register_component(LayoutComponentKind kind, Factory factory);
    [[nodiscard]] QWidget* build(const LayoutNode& node, const LayoutBuildContext& context,
        QWidget* parent = nullptr) const;

private:
    struct Attachment {
        QWidget* placeholder;
        QWidget* widget;
        LayoutNode node;
    };

    [[nodiscard]] QWidget* build_node(const LayoutNode& node, const LayoutBuildContext& context,
        QWidget* parent, std::vector<Attachment>& attachments) const;

    std::map<LayoutComponentKind, Factory> factories_;
    ViewRegistry views_;
};

} // namespace wiola::gui
