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

#pragma once
#include <QPointer>
#include <QWidget>
class QLabel;

namespace wiola::gui {

class ViewRegistry;

/// Shows operation outcomes independently of the visible document and transfer destination.
class StatusView final : public QWidget {
public:
    explicit StatusView(QWidget* parent = nullptr);
    void show_message(const QString& text);
    void show_context(const QString& document, const QString& destination);
    void register_views(ViewRegistry& registry);

private:
    QPointer<QLabel> message_, document_, destination_;
};

} // namespace wiola::gui
