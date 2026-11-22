/**
 * @file
 * @brief Transactional layout document edits with bounded undo history.
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

#include <cstddef>
#include <vector>

namespace wiola::gui {

class LayoutEditor final {
public:
    explicit LayoutEditor(LayoutDocument document);

    [[nodiscard]] const LayoutDocument& document() const noexcept { return document_; }

    [[nodiscard]] bool can_undo() const noexcept { return !undo_.empty(); }

    [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }

    bool insert(const QString& parent, std::size_t index, const LayoutNode& node);
    bool remove(const QString& id);
    bool move(const QString& id, const QString& parent, std::size_t index);
    bool configure(const QString& id, const LayoutNode& configuration);

    bool undo();
    bool redo();

private:
    bool commit(LayoutDocument next);

    LayoutDocument document_;
    std::vector<LayoutDocument> undo_;
    std::vector<LayoutDocument> redo_;
};

} // namespace wiola::gui
