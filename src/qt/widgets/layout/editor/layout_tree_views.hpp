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

#include <QListWidget>
#include <QTreeWidget>

#include <functional>
#include <variant>

namespace wiola::gui {

struct LayoutDrop {
    std::variant<PlayerAction, QString> item;
    QPoint position;
};

class ComponentPalette final : public QListWidget {
public:
    explicit ComponentPalette(QWidget* parent = nullptr);

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
};

class LayoutHierarchy final : public QTreeWidget {
public:
    explicit LayoutHierarchy(QWidget* parent = nullptr);

    void on_drop(std::function<void(LayoutDrop)> observer) { observer_ = std::move(observer); }

    void show_document(const LayoutDocument& document, const QString& selected);
    [[nodiscard]] QString selected_id() const;

protected:
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    std::function<void(LayoutDrop)> observer_;
};

} // namespace wiola::gui
