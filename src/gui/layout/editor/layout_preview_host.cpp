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

#include "layout_preview_host.hpp"
#include <gui/layout/layout_builder.hpp>

namespace wiola::gui {

LayoutPreviewHost::LayoutPreviewHost(QWidget* parent)
    : QGroupBox{"Isolated preview", parent}
    , layout_{new QVBoxLayout{this}}
{
    setObjectName("isolatedLayoutPreview");
}

bool LayoutPreviewHost::show_document(const LayoutDocument& document)
{
    auto candidate = std::make_unique<QWidget>();
    auto* actions = new PlayerActions{*candidate};
    actions->setParent(candidate.get());
    auto prepared = LayoutBuilder{}.prepare(document.root,
        {.actions = actions, .isolated_preview = true}, candidate.get());
    if (!prepared)
        return false;
    auto* built = prepared->attach(candidate.get());
    if (!built)
        return false;
    auto* layout = new QVBoxLayout{candidate.get()};
    layout->addWidget(built);
    delete preview_.data();
    preview_ = candidate.release();
    layout_->addWidget(preview_);
    return true;
}

} // namespace wiola::gui
