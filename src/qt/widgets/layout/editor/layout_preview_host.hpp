/**
 * @file
 * @brief Interactive palette, hierarchy, and inspector for button layouts.
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

#include <qt/widgets/layout/layout_document.hpp>

#include <QGroupBox>
#include <QPointer>
#include <QVBoxLayout>

namespace wiola::gui {

/// Owns the preview tree and its isolated actions; it has no application command dependency.
class LayoutPreviewHost final : public QGroupBox {
public:
    explicit LayoutPreviewHost(QWidget* parent = nullptr);
    [[nodiscard]] bool show_document(const LayoutDocument& document);

private:
    QVBoxLayout* layout_;
    QPointer<QWidget> preview_;
};

} // namespace wiola::gui
