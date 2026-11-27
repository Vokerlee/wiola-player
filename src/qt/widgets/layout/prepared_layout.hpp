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

#include <memory>

namespace wiola::gui {

struct LayoutAttachment {
    QWidget* placeholder;
    QPointer<QWidget> widget;
    LayoutNode node;
    QPointer<QWidget> original_parent;
};

/// Preparation owns new widgets only. Abandonment and failed lifetime checks leave live views
/// intact.
class PreparedLayout final {
public:
    [[nodiscard]] const LayoutDocument& document() const { return document_; }

    [[nodiscard]] bool ready(QWidget* parent = nullptr) const;
    [[nodiscard]] QWidget* attach(QWidget* parent = nullptr);

private:
    friend class LayoutBuilder;

    PreparedLayout(std::unique_ptr<QWidget> root, std::vector<LayoutAttachment> attachments,
        ViewRegistry views, LayoutDocument document)
        : root_{std::move(root)}
        , attachments_{std::move(attachments)}
        , views_{std::move(views)}
        , document_{std::move(document)}
    {
    }

    std::unique_ptr<QWidget> root_;
    std::vector<LayoutAttachment> attachments_;
    ViewRegistry views_;
    LayoutDocument document_;
};

} // namespace wiola::gui
