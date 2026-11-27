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

#include "layout_editor_dialog.hpp"

#include <qt/widgets/layout/layout_builder.hpp>
#include <qt/widgets/layout/layout_tree.hpp>

#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMimeData>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace wiola::gui {

namespace {

constexpr int identity_role = Qt::UserRole;
constexpr int action_role = Qt::UserRole + 1;

bool container(LayoutComponentKind kind)
{
    return is_layout_container(kind);
}

using NodeLocation = LayoutNodeLocation<const LayoutNode>;

} // namespace

LayoutEditorDialog::LayoutEditorDialog(LayoutDocument document, QWidget* parent)
    : QDialog{parent}
    , editor_{std::move(document)}
{
    setObjectName("buttonLayoutEditor");
    setWindowTitle("Edit button layout");

    const QSize preferred{fontMetrics().averageCharWidth() * 152, fontMetrics().height() * 46};
    const auto* screen = parent ? parent->screen() : QGuiApplication::primaryScreen();
    resize(screen ? preferred.boundedTo(screen->availableGeometry().size() * 0.9) : preferred);

    palette_ = new ComponentPalette{this};

    hierarchy_ = new LayoutHierarchy{this};

    add_action_ = new QPushButton{"Add button", this};
    add_action_->setObjectName("addLayoutAction");
    add_action_->setToolTip("Add the chosen button at the selected position in Your layout");

    remove_ = new QPushButton{"Remove from layout", this};
    remove_->setObjectName("removeLayoutComponent");
    remove_->setToolTip("Remove the selected item and its children from Your layout");

    move_up_ = new QPushButton{"Move up", this};
    move_up_->setObjectName("moveLayoutItemUp");
    move_up_->setToolTip("Move the selected item before its previous neighbor (Alt+Up)");

    move_down_ = new QPushButton{"Move down", this};
    move_down_->setObjectName("moveLayoutItemDown");
    move_down_->setToolTip("Move the selected item after its next neighbor (Alt+Down)");

    insertion_hint_ = new QLabel{this};
    insertion_hint_->setObjectName("layoutInsertionHint");
    insertion_hint_->setWordWrap(true);

    container_kind_ = new QComboBox{this};
    container_kind_->setObjectName("layoutContainerType");

    for (const auto& descriptor : component_descriptors())
        if (descriptor.container)
            container_kind_->addItem(QString::fromLatin1(descriptor.label),
                static_cast<int>(descriptor.kind));

    add_container_ = new QPushButton{"Add section", this};
    add_container_->setObjectName("addLayoutContainer");
    add_container_->setToolTip("Add an empty section at the selected position in Your layout");

    auto* section_form = new QFormLayout;
    section_form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    section_form->addRow("Arrange as", container_kind_);

    inspector_ = new LayoutPropertyInspector{this};
    inspector_->on_changed([this] { configure_selected(); });

    undo_ = new QPushButton{"Undo", this};
    undo_->setObjectName("undoLayoutEdit");

    redo_ = new QPushButton{"Redo", this};
    redo_->setObjectName("redoLayoutEdit");

    status_ = new QLabel{this};
    status_->setObjectName("layoutEditorStatus");
    status_->setWordWrap(true);

    auto* history = new QHBoxLayout;
    history->addWidget(undo_);
    history->addWidget(redo_);
    history->addStretch();

    auto* buttons = new QDialogButtonBox{QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this};
    buttons->button(QDialogButtonBox::Apply)->setObjectName("applyLayoutEdit");
    buttons->button(QDialogButtonBox::Cancel)->setObjectName("cancelLayoutEdit");

    auto* left = new QVBoxLayout;
    left->addWidget(new QLabel{"Available buttons", this});
    left->addWidget(palette_, 1);
    left->addWidget(insertion_hint_);
    left->addWidget(add_action_);
    left->addSpacing(left->spacing());
    left->addWidget(new QLabel{"Add a section", this});
    left->addLayout(section_form);
    left->addWidget(add_container_);

    auto* center = new QVBoxLayout;
    center->addWidget(new QLabel{"Your layout", this});
    center->addWidget(hierarchy_, 1);

    auto* position_commands = new QHBoxLayout;
    position_commands->addWidget(move_up_);
    position_commands->addWidget(move_down_);
    center->addLayout(position_commands);
    center->addWidget(remove_);
    center->addLayout(history);

    preview_ = new LayoutPreviewHost{this};

    auto* right = new QVBoxLayout;
    right->addWidget(new QLabel{"Selected item", this});
    right->addWidget(inspector_);
    right->addStretch();

    auto* columns = new QSplitter{Qt::Horizontal, this};
    columns->setObjectName("layoutEditorColumns");
    columns->setAccessibleName("Resizable layout editing panels");
    columns->setChildrenCollapsible(false);

    const int character = fontMetrics().averageCharWidth();

    palette_->setMinimumWidth(character * 22);
    hierarchy_->setMinimumWidth(character * 40);
    inspector_->setMinimumWidth(character * 30);

    for (auto* layout : {left, center, right}) {
        auto* panel = new QWidget;
        layout->setSizeConstraint(QLayout::SetMinimumSize);
        panel->setLayout(layout);
        columns->addWidget(panel);
    }

    columns->setStretchFactor(1, 1);
    columns->setSizes({character * 26, character * 70, character * 40});

    auto* preview_scroll = new QScrollArea{this};
    preview_scroll->setObjectName("layoutPreviewScroll");
    preview_scroll->setAccessibleName("Scrollable isolated layout preview");
    preview_scroll->setWidgetResizable(true);
    preview_scroll->setWidget(preview_);

    auto* rows = new QSplitter{Qt::Vertical, this};
    rows->setObjectName("layoutEditorRows");
    rows->setChildrenCollapsible(false);
    rows->addWidget(columns);
    rows->addWidget(preview_scroll);
    rows->setStretchFactor(0, 1);
    rows->setSizes({fontMetrics().height() * 28, fontMetrics().height() * 12});

    auto* contents = new QVBoxLayout;
    contents->addWidget(rows);

    auto* root = new QVBoxLayout{this};
    auto* scroll = new QScrollArea{this};
    scroll->setObjectName("layoutEditorScroll");
    scroll->setAccessibleName("Scrollable layout editing panels");
    scroll->setWidgetResizable(true);

    auto* panels = new QWidget;
    panels->setLayout(contents);
    scroll->setWidget(panels);
    auto* guidance =
        new QLabel{"Choose a button, select its position in Your layout, then click Add button. "
                   "Drag items or use Move up / Move down to rearrange. Apply saves your changes.",
            this};
    guidance->setWordWrap(true);
    root->addWidget(guidance);
    root->addWidget(scroll, 1);
    root->addWidget(status_);
    root->addWidget(buttons);

    // Return commits the focused property; applying the whole layout is always explicit.
    for (auto* button : findChildren<QPushButton*>())
        button->setAutoDefault(false);

    connect(add_action_, &QPushButton::clicked, this, &LayoutEditorDialog::add_from_palette);
    connect(add_container_, &QPushButton::clicked, this, &LayoutEditorDialog::add_container);
    connect(remove_, &QPushButton::clicked, this, &LayoutEditorDialog::remove_selected);
    connect(move_up_, &QPushButton::clicked, this, [this] { move_selected(-1); });
    connect(move_down_, &QPushButton::clicked, this, [this] { move_selected(1); });
    connect(palette_, &QListWidget::currentItemChanged, this, [this] { update_commands(); });
    connect(undo_, &QPushButton::clicked, this,
        [this] { finish_edit(editor_.undo(), selected_id()); });

    connect(redo_, &QPushButton::clicked, this,
        [this] { finish_edit(editor_.redo(), selected_id()); });

    connect(hierarchy_, &QTreeWidget::itemSelectionChanged, this,
        &LayoutEditorDialog::update_inspector);

    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
        if (!configure_selected())
            return;

        if (!apply_observer_ || apply_observer_(editor_.document()))
            accept();
        else
            status_->setText("Could not apply this layout");

        status_->setAccessibleDescription(status_->text());
    });

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    hierarchy_->on_drop([this](LayoutDrop request) { drop_component(std::move(request)); });

    auto* remove_shortcut = new QShortcut{QKeySequence::Delete, hierarchy_};
    remove_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(remove_shortcut, &QShortcut::activated, this, &LayoutEditorDialog::remove_selected);

    auto* undo_shortcut = new QShortcut{QKeySequence::Undo, hierarchy_};
    undo_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(undo_shortcut, &QShortcut::activated, this,
        [this] { finish_edit(editor_.undo(), selected_id()); });

    auto* redo_shortcut = new QShortcut{QKeySequence::Redo, hierarchy_};
    redo_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(redo_shortcut, &QShortcut::activated, this,
        [this] { finish_edit(editor_.redo(), selected_id()); });

    for (const int key : {Qt::Key_Up, Qt::Key_Down}) {
        const int offset = key == Qt::Key_Up ? -1 : 1;
        auto* move = new QShortcut{QKeySequence{Qt::ALT | key}, hierarchy_};
        move->setContext(Qt::WidgetWithChildrenShortcut);
        connect(move, &QShortcut::activated, this, [this, offset] { move_selected(offset); });
    }

    rebuild();
}

void LayoutEditorDialog::on_apply(ApplyObserver observer)
{
    apply_observer_ = std::move(observer);
}

QString LayoutEditorDialog::selected_id() const
{
    return hierarchy_->selected_id();
}

void LayoutEditorDialog::add_from_palette()
{
    QListWidgetItem* selected = palette_->currentItem();

    if (selected == nullptr)
        return;

    const auto action = parse_action(selected->data(action_role).toString());

    if (!action)
        return;

    QString id = selected_id();

    if (id.isEmpty())
        id = editor_.document().root.id;

    NodeLocation target = locate_layout_node(editor_.document().root, id);

    if (target.node == nullptr)
        return;

    if (container(target.node->kind)) {
        insert_action(*action, id, target.node->children.size());
    } else if (target.parent != nullptr) {
        insert_action(*action, target.parent->id, target.index + 1);
    }
}

void LayoutEditorDialog::insert_action(PlayerAction action, const QString& parent,
    std::size_t index)
{
    const QString base = action_name(action);
    QString id = base;

    for (int suffix = 2; locate_layout_node(editor_.document().root, id).node != nullptr; ++suffix)
        id = base + "-" + QString::number(suffix);

    LayoutNode node;
    node.id = id;
    node.kind = LayoutComponentKind::button;
    node.action = action;
    finish_edit(editor_.insert(parent, index, node), id);
}

void LayoutEditorDialog::add_container()
{
    NodeLocation target = locate_layout_node(editor_.document().root, selected_id());

    if (!target.node)
        return;

    const auto kind = static_cast<LayoutComponentKind>(container_kind_->currentData().toInt());
    const QString base = layout_component_name(kind);
    QString id = base;

    for (int suffix = 2; locate_layout_node(editor_.document().root, id).node; ++suffix)
        id = base + "-" + QString::number(suffix);

    LayoutNode node;
    node.id = id;
    node.kind = kind;

    if (container(target.node->kind))
        finish_edit(editor_.insert(target.node->id, target.node->children.size(), node), id);
    else if (target.parent)
        finish_edit(editor_.insert(target.parent->id, target.index + 1, node), id);
}

void LayoutEditorDialog::remove_selected()
{
    finish_edit(editor_.remove(selected_id()));
}

void LayoutEditorDialog::move_selected(int offset)
{
    const QString id = selected_id();
    NodeLocation location = locate_layout_node(editor_.document().root, id);

    if (location.parent == nullptr)
        return;

    const auto destination = static_cast<std::ptrdiff_t>(location.index) + offset;

    if (destination < 0 ||
        destination >= static_cast<std::ptrdiff_t>(location.parent->children.size()))
        return;

    finish_edit(editor_.move(id, location.parent->id, static_cast<std::size_t>(destination)), id);
}

void LayoutEditorDialog::drop_component(LayoutDrop request)
{
    QTreeWidgetItem* item = hierarchy_->itemAt(request.position);
    const QString id =
        item != nullptr ? item->data(0, identity_role).toString() : editor_.document().root.id;
    NodeLocation target = locate_layout_node(editor_.document().root, id);

    if (target.node == nullptr)
        return;

    QString parent = id;
    std::size_t index = target.node->children.size();

    if (!container(target.node->kind)) {
        if (target.parent == nullptr)
            return;

        parent = target.parent->id;
        index = target.index + 1;
    }

    if (const auto* action = std::get_if<PlayerAction>(&request.item))
        insert_action(*action, parent, index);
    else {
        const auto& moved = std::get<QString>(request.item);
        finish_edit(editor_.move(moved, parent, index), moved);
    }
}

bool LayoutEditorDialog::configure_selected()
{
    const auto location = locate_layout_node(editor_.document().root, selected_id());

    if (!location.node)
        return true;

    const auto configured = inspector_->configuration(*location.node);

    if (!configured) {
        status_->setText(inspector_->error());
        return false;
    }

    if (*configured == *location.node)
        return true;

    const auto id = location.node->id;
    const bool succeeded = editor_.configure(id, *configured);
    finish_edit(succeeded, id);

    return succeeded;
}

void LayoutEditorDialog::rebuild(const QString& selected)
{
    hierarchy_->show_document(editor_.document(), selected);
    update_inspector();
    update_preview();
    update_history();
}

void LayoutEditorDialog::update_inspector()
{
    inspector_->show_node(locate_layout_node(editor_.document().root, selected_id()).node);
    update_commands();
}

void LayoutEditorDialog::update_commands()
{
    const auto location = locate_layout_node(editor_.document().root, selected_id());
    const bool can_insert = location.node && (container(location.node->kind) || location.parent);

    add_action_->setEnabled(can_insert && palette_->currentItem());
    add_container_->setEnabled(can_insert);
    remove_->setEnabled(location.parent);
    move_up_->setEnabled(location.parent && location.index > 0);
    move_down_->setEnabled(location.parent &&
        location.index + 1 < location.parent->children.size());

    const auto* selected = hierarchy_->currentItem();
    const QString label = selected ? selected->text(0) : QString{};
    if (!can_insert)
        insertion_hint_->setText("Select a position in Your layout");
    else if (container(location.node->kind))
        insertion_hint_->setText("Add to: " + label);
    else
        insertion_hint_->setText("Insert after: " + label);

    insertion_hint_->setAccessibleDescription(insertion_hint_->text());
}

void LayoutEditorDialog::update_preview()
{
    if (!preview_->show_document(editor_.document()))
        status_->setText("Could not construct the isolated preview");
}

void LayoutEditorDialog::update_history()
{
    undo_->setEnabled(editor_.can_undo());
    redo_->setEnabled(editor_.can_redo());
}

void LayoutEditorDialog::finish_edit(bool succeeded, const QString& selected)
{
    if (!succeeded) {
        status_->setText("That layout change is not valid here");
        return;
    }

    status_->clear();
    rebuild(selected);
}

} // namespace wiola::gui
