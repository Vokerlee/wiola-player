/**
 * @file
 * @brief Shared actions for every existing player command.
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

#include "player_shortcuts.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QKeyEvent>
#include <QWidget>

namespace wiola::gui {

PlayerShortcuts::PlayerShortcuts(QWidget& context, QObject* parent)
    : QObject{parent}
    , context_{&context}
{
    qApp->installEventFilter(this);
}

bool PlayerShortcuts::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride && context_) {
        auto* button = qobject_cast<QAbstractButton*>(watched);
        auto* key = static_cast<QKeyEvent*>(event);
        if (button && button->isEnabled() && button->window() == context_->window() &&
            key->key() == Qt::Key_Space && key->modifiers() == Qt::NoModifier) {
            key->accept();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

} // namespace wiola::gui
