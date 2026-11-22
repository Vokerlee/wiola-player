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

#include "action_button_binding.hpp"
#include <QPointer>
#include <QSignalBlocker>

namespace wiola::gui {

ActionButtonBinding::ActionButtonBinding(QAbstractButton& button, QAction& action,
    ActionButtonPresentation presentation)
    : QObject{&button}
{
    button.setFocusPolicy(Qt::StrongFocus);
    const auto refresh = [&button, &action, presentation] {
        const QSignalBlocker blocked{&button};
        button.setEnabled(action.isEnabled());
        button.setCheckable(action.isCheckable());
        button.setChecked(action.isChecked());
        button.setText(presentation == ActionButtonPresentation::icon_only ? QString{}
                                                                           : action.text());
        button.setIcon(action.icon());
        button.setToolTip(action.toolTip());
        button.setStatusTip(action.statusTip());
        button.setAccessibleName(action.text());
        button.setAccessibleDescription(action.toolTip());
    };
    refresh();
    connect(&action, &QAction::changed, this, refresh);
    connect(&action, &QObject::destroyed, this, [&button] { button.setEnabled(false); });
    connect(&button, &QAbstractButton::clicked, this, [command = QPointer<QAction>{&action}] {
        if (command && command->isEnabled())
            command->trigger();
    });
}

} // namespace wiola::gui
