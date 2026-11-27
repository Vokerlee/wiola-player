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

#include "view_reference.hpp"

#include <QPointer>
#include <QWidget>

#include <map>

namespace wiola::gui {

struct ViewRegistration {
    QPointer<QWidget> widget;
    LayoutComponentKind kind;
    bool allow_structure;
};

/// Borrows live views. Neither registration nor failed layout preparation transfers ownership.
class ViewRegistry final {
public:
    [[nodiscard]] bool add(ViewReference reference, QWidget* widget, LayoutComponentKind kind,
        bool allow_structure = false);

    void remove(const ViewReference& reference) { views_.erase(reference); }

    [[nodiscard]] const ViewRegistration* find(const ViewReference& reference) const;

    [[nodiscard]] const std::map<ViewReference, ViewRegistration>& entries() const
    {
        return views_;
    }

private:
    std::map<ViewReference, ViewRegistration> views_;
};

} // namespace wiola::gui
