/**
 * @file
 * @brief A length of time, written the way a listener reads it.
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

#include "status_view.hpp"
#include <QLabel>
#include <QVBoxLayout>
#include <gui/layout/view_registry.hpp>
#include <stdexcept>

namespace wiola::gui {

StatusView::StatusView(QWidget* parent)
    : QWidget{parent}
{
    auto* body = new QVBoxLayout{this};
    body->setContentsMargins(0, 0, 0, 0);
    document_ = new QLabel{this};
    destination_ = new QLabel{this};
    message_ = new QLabel{this};
    for (auto* label : {document_.data(), destination_.data(), message_.data()}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        body->addWidget(label);
    }
    message_->setAccessibleName("Player status");
    document_->setAccessibleName("Visible document");
    destination_->setAccessibleName("Transfer destination");
}

void StatusView::show_message(const QString& text)
{
    message_->setText(text);
    message_->setAccessibleDescription(text);
}

void StatusView::show_context(const QString& document, const QString& destination)
{
    document_->setText("Viewing: " + document);
    destination_->setText("Transfer destination: " + destination);
}

void StatusView::register_views(ViewRegistry& registry)
{
    const auto add = [&](const char* key, QWidget* widget, LayoutComponentKind kind,
                         bool structure = false) {
        if (!registry.add(component_view(key), widget, kind, structure))
            throw std::logic_error{"duplicate status view"};
    };
    add("statusSection", this, LayoutComponentKind::column, true);
    add("playerStatus", message_, LayoutComponentKind::status);
    add("visibleDocument", document_, LayoutComponentKind::status);
    add("activeDestination", destination_, LayoutComponentKind::status);
}

} // namespace wiola::gui
