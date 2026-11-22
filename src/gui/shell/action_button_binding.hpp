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

#pragma once
#include <QAbstractButton>
#include <QAction>
#include <QObject>

namespace wiola::gui {

enum class ActionButtonPresentation {
    text,
    icon_only,
};

/// Binding lifetime follows its button; shared actions outlive replaceable layout trees.
class ActionButtonBinding final : public QObject {
public:
    ActionButtonBinding(QAbstractButton& button, QAction& action,
        ActionButtonPresentation presentation = ActionButtonPresentation::text);
};

} // namespace wiola::gui
