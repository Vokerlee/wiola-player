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
#include <QGuiApplication>
#include <QScreen>
#include <QScrollArea>
#include <qt/widgets/layout/layout_tree.hpp>

#include <qt/widgets/layout/layout_builder.hpp>

#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMimeData>
#include <QShortcut>
#include <QSignalBlocker>
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
    const QSize preferred{fontMetrics().averageCharWidth() * 110, fontMetrics().height() * 32};
    resize(QGuiApplication::primaryScreen()
            ? preferred.boundedTo(QGuiApplication::primaryScreen()->availableGeometry().size())
            : preferred);

    palette_ = new ComponentPalette{this};
    hierarchy_ = new LayoutHierarchy{this};
    auto* add = new QPushButton{"Add action", this};
    add->setObjectName("addLayoutAction");
    auto* remove = new QPushButton{"Remove", this};
    remove->setObjectName("removeLayoutComponent");
    auto* palette_commands = new QHBoxLayout;
    palette_commands->addWidget(add);
    palette_commands->addWidget(remove);

    container_kind_ = new QComboBox{this};
    container_kind_->setObjectName("layoutContainerType");
    for (const auto& descriptor : component_descriptors())
        if (descriptor.container)
            container_kind_->addItem(QString::fromLatin1(descriptor.key),
                static_cast<int>(descriptor.kind));
    auto* add_container_button = new QPushButton{"Add container", this};
    add_container_button->setObjectName("addLayoutContainer");

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
    left->addWidget(new QLabel{"Actions", this});
    left->addWidget(palette_);
    left->addLayout(palette_commands);
    left->addWidget(container_kind_);
    left->addWidget(add_container_button);

    auto* center = new QVBoxLayout;
    center->addWidget(new QLabel{"Layout", this});
    center->addWidget(hierarchy_);
    center->addLayout(history);

    preview_ = new LayoutPreviewHost{this};
    auto* right = new QVBoxLayout;
    right->addWidget(new QLabel{"Inspector", this});
    right->addWidget(inspector_);
    right->addWidget(preview_, 1);

    auto* contents = new QHBoxLayout;
    contents->addLayout(left, 1);
    contents->addLayout(center, 2);
    contents->addLayout(right, 1);

    auto* root = new QVBoxLayout{this};
    auto* scroll = new QScrollArea{this};
    scroll->setObjectName("layoutEditorScroll");
    scroll->setAccessibleName("Scrollable layout editing panels");
    scroll->setWidgetResizable(true);
    auto* panels = new QWidget;
    panels->setLayout(contents);
    scroll->setWidget(panels);
    root->addWidget(scroll, 1);
    root->addWidget(status_);
    root->addWidget(buttons);

    connect(add, &QPushButton::clicked, this, &LayoutEditorDialog::add_from_palette);
    connect(add_container_button, &QPushButton::clicked, this, &LayoutEditorDialog::add_container);
    connect(remove, &QPushButton::clicked, this, &LayoutEditorDialog::remove_selected);
    connect(undo_, &QPushButton::clicked, this,
        [this] { finish_edit(editor_.undo(), selected_id()); });
    connect(redo_, &QPushButton::clicked, this,
        [this] { finish_edit(editor_.redo(), selected_id()); });
    connect(hierarchy_, &QTreeWidget::itemSelectionChanged, this,
        &LayoutEditorDialog::update_inspector);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
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

void LayoutEditorDialog::configure_selected()
{
    const auto location = locate_layout_node(editor_.document().root, selected_id());
    if (!location.node)
        return;
    const auto configured = inspector_->configuration(*location.node);
    if (!configured) {
        status_->setText(inspector_->error());
        return;
    }
    const auto id = location.node->id;
    finish_edit(editor_.configure(id, *configured), id);
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
