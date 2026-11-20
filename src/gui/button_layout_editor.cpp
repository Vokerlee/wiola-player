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

#include "button_layout_editor.hpp"

#include "layout_builder.hpp"

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

constexpr auto component_mime = "application/x-wiola-layout-component";
constexpr int identity_role = Qt::UserRole;
constexpr int action_role = Qt::UserRole + 1;

struct ActionName {
    PlayerAction action;
    const char* name;
    const char* label;
};

constexpr std::array available_actions{
    ActionName{PlayerAction::open,      "open",      "Open tracks"   },
    ActionName{PlayerAction::previous,  "previous",  "Previous track"},
    ActionName{PlayerAction::play,      "play",      "Play / pause"  },
    ActionName{PlayerAction::next,      "next",      "Next track"    },
    ActionName{PlayerAction::stop,      "stop",      "Stop"          },
    ActionName{PlayerAction::repeat,    "repeat",    "Repeat"        },
    ActionName{PlayerAction::shuffle,   "shuffle",   "Shuffle"       },
    ActionName{PlayerAction::add,       "add",       "Add tracks"    },
    ActionName{PlayerAction::remove,    "remove",    "Remove tracks" },
    ActionName{PlayerAction::clear,     "clear",     "Clear queue"   },
    ActionName{PlayerAction::equalizer, "equalizer", "Equalizer"     },
};

std::optional<PlayerAction> parse_action(const QString& name)
{
    for (const ActionName& entry : available_actions) {
        if (name == entry.name)
            return entry.action;
    }

    return std::nullopt;
}

QString action_name(PlayerAction action)
{
    for (const ActionName& entry : available_actions) {
        if (entry.action == action)
            return QString::fromLatin1(entry.name);
    }

    return {};
}

bool container(LayoutComponentKind kind)
{
    return is_layout_container(kind);
}

struct NodeLocation {
    const LayoutNode* parent{nullptr};
    const LayoutNode* node{nullptr};
    std::size_t index{0};
};

NodeLocation locate(const LayoutNode& root, const QString& id, const LayoutNode* parent = nullptr)
{
    if (root.id == id)
        return {.parent = parent, .node = &root, .index = 0};

    for (const LayoutNode& child : root.children) {
        if (child.id == id) {
            return {.parent = &root,
                .node = &child,
                .index = static_cast<std::size_t>(&child - root.children.data())};
        }

        if (NodeLocation found = locate(child, id, &root); found.node != nullptr)
            return found;
    }

    return {};
}

QMimeData* component_data(const QString& payload)
{
    auto* data = new QMimeData;
    data->setData(component_mime, payload.toUtf8());
    data->setText(payload);
    return data;
}

class ComponentPalette final : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override
    {
        if (items.empty())
            return nullptr;

        return component_data("action:" + items.front()->data(action_role).toString());
    }
};

class LayoutHierarchy final : public QTreeWidget {
public:
    using DropObserver = std::function<void(const QString&, const QPoint&)>;
    using QTreeWidget::QTreeWidget;

    void on_drop(DropObserver observer) { observer_ = std::move(observer); }

protected:
    QStringList mimeTypes() const override { return {QString::fromLatin1(component_mime)}; }

    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override
    {
        if (items.empty())
            return nullptr;

        return component_data("node:" + items.front()->data(0, identity_role).toString());
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData()->hasFormat(component_mime))
            event->acceptProposedAction();
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (event->mimeData()->hasFormat(component_mime))
            event->acceptProposedAction();
    }

    void dropEvent(QDropEvent* event) override
    {
        if (!event->mimeData()->hasFormat(component_mime) || !observer_)
            return;

        observer_(QString::fromUtf8(event->mimeData()->data(component_mime)),
            event->position().toPoint());
        event->acceptProposedAction();
    }

private:
    DropObserver observer_;
};

} // namespace

ButtonLayoutEditor::ButtonLayoutEditor(LayoutDocument document, QWidget* parent)
    : QDialog{parent}
    , editor_{std::move(document)}
{
    setObjectName("buttonLayoutEditor");
    setWindowTitle("Edit button layout");
    setMinimumSize(900, 560);

    palette_ = new ComponentPalette{this};
    palette_->setObjectName("layoutComponentPalette");
    palette_->setDragEnabled(true);
    palette_->setDragDropMode(QAbstractItemView::DragOnly);
    palette_->setAccessibleName("Button action palette");

    for (const ActionName& entry : available_actions) {
        auto* item = new QListWidgetItem{entry.label, palette_};
        item->setData(action_role, entry.name);
        item->setToolTip("Drag onto a container, or select it and press Add");
    }

    hierarchy_ = new LayoutHierarchy{this};
    hierarchy_->setObjectName("layoutHierarchy");
    hierarchy_->setHeaderLabels({"Component", "Type"});
    hierarchy_->setSelectionMode(QAbstractItemView::SingleSelection);
    hierarchy_->setDragEnabled(true);
    hierarchy_->setAcceptDrops(true);
    hierarchy_->setDropIndicatorShown(true);
    hierarchy_->setDragDropMode(QAbstractItemView::DragDrop);
    hierarchy_->setAccessibleName("Layout component hierarchy");

    auto* add = new QPushButton{"Add action", this};
    add->setObjectName("addLayoutAction");
    auto* remove = new QPushButton{"Remove", this};
    remove->setObjectName("removeLayoutComponent");
    auto* palette_commands = new QHBoxLayout;
    palette_commands->addWidget(add);
    palette_commands->addWidget(remove);

    container_kind_ = new QComboBox{this};
    container_kind_->setObjectName("layoutContainerType");
    for (const auto kind :
        {LayoutComponentKind::row, LayoutComponentKind::column, LayoutComponentKind::group,
            LayoutComponentKind::splitter, LayoutComponentKind::grid, LayoutComponentKind::tabs})
        container_kind_->addItem(layout_component_name(kind), static_cast<int>(kind));
    auto* add_container_button = new QPushButton{"Add container", this};
    add_container_button->setObjectName("addLayoutContainer");

    title_ = new QLineEdit{this};
    title_->setObjectName("layoutComponentTitle");
    width_ = new QSpinBox{this};
    width_->setObjectName("layoutComponentWidth");
    width_->setRange(0, 4096);
    width_->setSpecialValueText("Automatic");
    action_ = new QComboBox{this};
    action_->setObjectName("layoutComponentAction");

    for (const ActionName& entry : available_actions)
        action_->addItem(entry.label, entry.name);

    orientation_ = new QComboBox{this};
    orientation_->setObjectName("layoutComponentOrientation");
    orientation_->addItem("Horizontal", static_cast<int>(Qt::Horizontal));
    orientation_->addItem("Vertical", static_cast<int>(Qt::Vertical));
    columns_ = new QSpinBox{this};
    columns_->setObjectName("layoutGridColumns");
    columns_->setRange(1, 32);
    sizes_ = new QLineEdit{this};
    sizes_->setObjectName("layoutSplitterSizes");
    sizes_->setPlaceholderText("Automatic, or one positive weight per pane");
    sizes_->setMaxLength(256);

    auto* inspector = new QFormLayout;
    inspector->addRow("Title", title_);
    inspector->addRow("Fixed width", width_);
    inspector->addRow("Action", action_);
    inspector->addRow("Orientation", orientation_);
    inspector->addRow("Grid columns", columns_);
    inspector->addRow("Splitter weights", sizes_);

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

    auto* preview_group = new QGroupBox{"Isolated preview", this};
    preview_group->setObjectName("isolatedLayoutPreview");
    preview_layout_ = new QVBoxLayout{preview_group};

    auto* right = new QVBoxLayout;
    right->addWidget(new QLabel{"Inspector", this});
    right->addLayout(inspector);
    right->addWidget(preview_group, 1);

    auto* contents = new QHBoxLayout;
    contents->addLayout(left, 1);
    contents->addLayout(center, 2);
    contents->addLayout(right, 1);

    auto* root = new QVBoxLayout{this};
    root->addLayout(contents);
    root->addWidget(status_);
    root->addWidget(buttons);

    connect(add, &QPushButton::clicked, this, &ButtonLayoutEditor::add_from_palette);
    connect(add_container_button, &QPushButton::clicked, this, &ButtonLayoutEditor::add_container);
    connect(remove, &QPushButton::clicked, this, &ButtonLayoutEditor::remove_selected);
    connect(undo_, &QPushButton::clicked, this,
        [this] { finish_edit(editor_.undo(), selected_id()); });
    connect(redo_, &QPushButton::clicked, this,
        [this] { finish_edit(editor_.redo(), selected_id()); });
    connect(hierarchy_, &QTreeWidget::itemSelectionChanged, this,
        &ButtonLayoutEditor::update_inspector);
    connect(title_, &QLineEdit::editingFinished, this, &ButtonLayoutEditor::configure_selected);
    connect(width_, &QSpinBox::editingFinished, this, &ButtonLayoutEditor::configure_selected);
    connect(action_, &QComboBox::activated, this, [this](int) { configure_selected(); });
    connect(orientation_, &QComboBox::activated, this, [this](int) { configure_selected(); });
    connect(columns_, &QSpinBox::editingFinished, this, &ButtonLayoutEditor::configure_selected);
    connect(sizes_, &QLineEdit::editingFinished, this, &ButtonLayoutEditor::configure_selected);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
        if (!apply_observer_ || apply_observer_(editor_.document()))
            accept();
        else
            status_->setText("Could not apply this layout");
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    static_cast<LayoutHierarchy*>(hierarchy_)
        ->on_drop([this](const QString& payload, const QPoint& position) {
            drop_component(payload, position);
        });

    auto* remove_shortcut = new QShortcut{QKeySequence::Delete, this};
    connect(remove_shortcut, &QShortcut::activated, this, &ButtonLayoutEditor::remove_selected);
    auto* undo_shortcut = new QShortcut{QKeySequence::Undo, this};
    connect(undo_shortcut, &QShortcut::activated, this,
        [this] { finish_edit(editor_.undo(), selected_id()); });
    auto* redo_shortcut = new QShortcut{QKeySequence::Redo, this};
    connect(redo_shortcut, &QShortcut::activated, this,
        [this] { finish_edit(editor_.redo(), selected_id()); });

    for (const int key : {Qt::Key_Up, Qt::Key_Down}) {
        const int offset = key == Qt::Key_Up ? -1 : 1;
        auto* move = new QShortcut{QKeySequence{Qt::ALT | key}, this};
        connect(move, &QShortcut::activated, this, [this, offset] { move_selected(offset); });
    }

    rebuild();
}

void ButtonLayoutEditor::on_apply(ApplyObserver observer)
{
    apply_observer_ = std::move(observer);
}

QString ButtonLayoutEditor::selected_id() const
{
    const QList<QTreeWidgetItem*> selected = hierarchy_->selectedItems();
    return selected.empty() ? QString{} : selected.front()->data(0, identity_role).toString();
}

void ButtonLayoutEditor::add_from_palette()
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

    NodeLocation target = locate(editor_.document().root, id);
    if (target.node == nullptr)
        return;

    if (container(target.node->kind)) {
        insert_action(*action, id, target.node->children.size());
    } else if (target.parent != nullptr) {
        insert_action(*action, target.parent->id, target.index + 1);
    }
}

void ButtonLayoutEditor::insert_action(PlayerAction action, const QString& parent,
    std::size_t index)
{
    const QString base = action_name(action);
    QString id = base;

    for (int suffix = 2; locate(editor_.document().root, id).node != nullptr; ++suffix)
        id = base + "-" + QString::number(suffix);

    LayoutNode node;
    node.id = id;
    node.kind = LayoutComponentKind::button;
    node.action = action;
    finish_edit(editor_.insert(parent, index, node), id);
}

void ButtonLayoutEditor::add_container()
{
    NodeLocation target = locate(editor_.document().root, selected_id());
    if (!target.node)
        return;
    const auto kind = static_cast<LayoutComponentKind>(container_kind_->currentData().toInt());
    const QString base = layout_component_name(kind);
    QString id = base;
    for (int suffix = 2; locate(editor_.document().root, id).node; ++suffix)
        id = base + "-" + QString::number(suffix);
    LayoutNode node;
    node.id = id;
    node.kind = kind;
    if (container(target.node->kind))
        finish_edit(editor_.insert(target.node->id, target.node->children.size(), node), id);
    else if (target.parent)
        finish_edit(editor_.insert(target.parent->id, target.index + 1, node), id);
}

void ButtonLayoutEditor::remove_selected()
{
    finish_edit(editor_.remove(selected_id()));
}

void ButtonLayoutEditor::move_selected(int offset)
{
    const QString id = selected_id();
    NodeLocation location = locate(editor_.document().root, id);
    if (location.parent == nullptr)
        return;

    const auto destination = static_cast<std::ptrdiff_t>(location.index) + offset;
    if (destination < 0 ||
        destination >= static_cast<std::ptrdiff_t>(location.parent->children.size()))
        return;

    finish_edit(editor_.move(id, location.parent->id, static_cast<std::size_t>(destination)), id);
}

void ButtonLayoutEditor::drop_component(const QString& payload, const QPoint& position)
{
    QTreeWidgetItem* item = hierarchy_->itemAt(position);
    const QString id =
        item != nullptr ? item->data(0, identity_role).toString() : editor_.document().root.id;
    NodeLocation target = locate(editor_.document().root, id);
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

    if (payload.startsWith("action:")) {
        if (const auto action = parse_action(payload.mid(7)))
            insert_action(*action, parent, index);
    } else if (payload.startsWith("node:")) {
        const QString moved = payload.mid(5);
        finish_edit(editor_.move(moved, parent, index), moved);
    }
}

void ButtonLayoutEditor::configure_selected()
{
    if (updating_)
        return;

    NodeLocation location = locate(editor_.document().root, selected_id());
    if (location.node == nullptr)
        return;

    LayoutNode configured = *location.node;
    configured.title = title_->text();
    configured.width = width_->value() == 0 ? std::optional<int>{} : width_->value();

    if (configured.kind == LayoutComponentKind::button)
        configured.action = parse_action(action_->currentData().toString());

    if (configured.kind == LayoutComponentKind::splitter) {
        configured.orientation = static_cast<Qt::Orientation>(orientation_->currentData().toInt());
        configured.sizes.clear();
        if (!sizes_->text().trimmed().isEmpty()) {
            for (const QString& part : sizes_->text().split(',')) {
                bool valid = false;
                const int value = part.trimmed().toInt(&valid);
                if (!valid || value < 1 || value > 4096) {
                    status_->setText(
                        "Use positive splitter weights from 1 to 4096, separated by commas"
                    );
                    return;
                }
                configured.sizes.push_back(value);
            }
        }
    }
    if (configured.kind == LayoutComponentKind::grid)
        configured.columns = columns_->value();

    finish_edit(editor_.configure(location.node->id, configured), location.node->id);
}

void ButtonLayoutEditor::rebuild(const QString& selected)
{
    updating_ = true;
    hierarchy_->clear();

    const auto add_node = [this](auto&& self, const LayoutNode& node,
                              QTreeWidgetItem* parent) -> void {
        auto* item = new QTreeWidgetItem{
            {node.id, layout_component_name(node.kind)}
        };
        item->setData(0, identity_role, node.id);

        if (parent != nullptr)
            parent->addChild(item);
        else
            hierarchy_->addTopLevelItem(item);

        for (const LayoutNode& child : node.children)
            self(self, child, item);
    };

    add_node(add_node, editor_.document().root, nullptr);
    hierarchy_->expandAll();

    const QList<QTreeWidgetItem*> items =
        hierarchy_->findItems(selected, Qt::MatchExactly | Qt::MatchRecursive);
    if (!items.empty())
        hierarchy_->setCurrentItem(items.front());
    else
        hierarchy_->setCurrentItem(hierarchy_->topLevelItem(0));

    updating_ = false;
    update_inspector();
    update_preview();
    update_history();
}

void ButtonLayoutEditor::update_inspector()
{
    updating_ = true;
    NodeLocation location = locate(editor_.document().root, selected_id());
    const bool present = location.node != nullptr;
    const bool button = present && location.node->kind == LayoutComponentKind::button;
    title_->setEnabled(present);
    width_->setEnabled(present);
    action_->setEnabled(button);
    const bool splitter = present && location.node->kind == LayoutComponentKind::splitter;
    orientation_->setEnabled(splitter);
    sizes_->setEnabled(splitter);
    columns_->setEnabled(present && location.node->kind == LayoutComponentKind::grid);

    if (present) {
        title_->setText(location.node->title);
        width_->setValue(location.node->width.value_or(0));
        const int index =
            action_->findData(action_name(location.node->action.value_or(PlayerAction::count)));
        action_->setCurrentIndex(index);
        orientation_->setCurrentIndex(orientation_->findData(
            static_cast<int>(location.node->orientation.value_or(Qt::Horizontal))
        ));
        columns_->setValue(location.node->columns.value_or(1));
        QStringList weights;
        for (int weight : location.node->sizes)
            weights.append(QString::number(weight));
        sizes_->setText(weights.join(", "));
    } else {
        title_->clear();
        width_->setValue(0);
        action_->setCurrentIndex(-1);
        orientation_->setCurrentIndex(0);
        columns_->setValue(1);
        sizes_->clear();
    }

    updating_ = false;
}

void ButtonLayoutEditor::update_preview()
{
    delete preview_;
    preview_ = LayoutBuilder{}.build(editor_.document().root, {}, preview_layout_->parentWidget());

    if (preview_ == nullptr) {
        status_->setText("Could not construct the isolated preview");
        return;
    }

    preview_layout_->addWidget(preview_);

    const auto present_node = [this](auto&& self, const LayoutNode& node) -> void {
        if (node.action) {
            if (auto* button = preview_->findChild<QPushButton*>(node.id)) {
                const QString name = action_name(*node.action);
                const auto found = std::ranges::find(available_actions, name, &ActionName::name);
                button->setText(
                    found == available_actions.end() ? name : QString::fromLatin1(found->label)
                );
            }
        }

        for (const LayoutNode& child : node.children)
            self(self, child);
    };

    present_node(present_node, editor_.document().root);
}

void ButtonLayoutEditor::update_history()
{
    undo_->setEnabled(editor_.can_undo());
    redo_->setEnabled(editor_.can_redo());
}

void ButtonLayoutEditor::finish_edit(bool succeeded, const QString& selected)
{
    if (!succeeded) {
        status_->setText("That layout change is not valid here");
        return;
    }

    status_->clear();
    rebuild(selected);
}

} // namespace wiola::gui
