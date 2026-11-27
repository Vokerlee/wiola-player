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

#include "layout_preview_host.hpp"
#include "layout_property_inspector.hpp"
#include "layout_tree_views.hpp"

#include <qt/widgets/layout/layout_editor.hpp>

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>

#include <functional>

class QLayout;

namespace wiola::gui {

class LayoutEditorDialog final : public QDialog {
public:
    using ApplyObserver = std::function<bool(const LayoutDocument&)>;

    explicit LayoutEditorDialog(LayoutDocument document, QWidget* parent = nullptr);

    void on_apply(ApplyObserver observer);

    [[nodiscard]] const LayoutDocument& document() const noexcept { return editor_.document(); }

private:
    void add_from_palette();
    void add_container();
    void remove_selected();
    void move_selected(int offset);
    void drop_component(LayoutDrop request);
    void insert_action(PlayerAction action, const QString& parent, std::size_t index);
    bool configure_selected();
    void rebuild(const QString& selected = {});
    void update_inspector();
    void update_commands();
    void update_preview();
    void update_history();
    void finish_edit(bool succeeded, const QString& selected = {});
    [[nodiscard]] QString selected_id() const;

    LayoutEditor editor_;
    ComponentPalette* palette_{nullptr};
    LayoutHierarchy* hierarchy_{nullptr};
    LayoutPropertyInspector* inspector_{nullptr};
    QComboBox* container_kind_{nullptr};
    QPushButton* add_action_{nullptr};
    QPushButton* add_container_{nullptr};
    QPushButton* remove_{nullptr};
    QPushButton* move_up_{nullptr};
    QPushButton* move_down_{nullptr};
    QLabel* insertion_hint_{nullptr};
    QPushButton* undo_{nullptr};
    QPushButton* redo_{nullptr};
    QLabel* status_{nullptr};
    LayoutPreviewHost* preview_{nullptr};
    ApplyObserver apply_observer_;
};

} // namespace wiola::gui
