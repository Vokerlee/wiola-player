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

#include "layout_property_inspector.hpp"
#include <QFormLayout>
#include <QStringList>

namespace wiola::gui {

LayoutPropertyInspector::LayoutPropertyInspector(QWidget* parent)
    : QWidget{parent}
{
    title_ = new QLineEdit{this};
    title_->setObjectName("layoutComponentTitle");
    width_ = new QSpinBox{this};
    width_->setObjectName("layoutComponentWidth");
    width_->setRange(0, 4096);
    width_->setSpecialValueText("Automatic");
    action_ = new QComboBox{this};
    action_->setObjectName("layoutComponentAction");
    for (const auto& entry : action_descriptors())
        action_->addItem(entry.label, entry.key);
    orientation_ = new QComboBox{this};
    orientation_->setObjectName("layoutComponentOrientation");
    orientation_->addItem("Horizontal", static_cast<int>(Qt::Horizontal));
    orientation_->addItem("Vertical", static_cast<int>(Qt::Vertical));
    orientation_->addItem("Automatic", 0);
    columns_ = new QSpinBox{this};
    columns_->setObjectName("layoutGridColumns");
    columns_->setRange(1, 32);
    sizes_ = new QLineEdit{this};
    sizes_->setObjectName("layoutSplitterSizes");
    sizes_->setPlaceholderText("Automatic, or one positive weight per pane");
    sizes_->setMaxLength(256);
    error_ = new QLabel{this};
    error_->setObjectName("layoutPropertyError");
    error_->setWordWrap(true);
    error_->setAccessibleName("Layout property error");
    auto* form = new QFormLayout{this};
    form->addRow("Title", title_);
    form->addRow("Fixed width", width_);
    form->addRow("Action", action_);
    form->addRow("Orientation", orientation_);
    form->addRow("Grid columns", columns_);
    form->addRow("Splitter weights", sizes_);
    form->addRow(error_);
    const auto changed = [this] {
        if (!updating_ && observer_)
            observer_();
    };
    connect(title_, &QLineEdit::editingFinished, this, changed);
    connect(width_, &QSpinBox::editingFinished, this, changed);
    connect(action_, &QComboBox::activated, this, [changed](int) { changed(); });
    connect(orientation_, &QComboBox::activated, this, [changed](int) { changed(); });
    connect(columns_, &QSpinBox::editingFinished, this, changed);
    connect(sizes_, &QLineEdit::editingFinished, this, changed);
}

void LayoutPropertyInspector::show_node(const LayoutNode* node)
{
    updating_ = true;
    error_->clear();
    title_->setEnabled(node);
    width_->setEnabled(node);
    action_->setEnabled(node && node->kind == LayoutComponentKind::button && !node->view);
    const bool splitter = node && node->kind == LayoutComponentKind::splitter;
    orientation_->setEnabled(splitter);
    sizes_->setEnabled(splitter);
    columns_->setEnabled(node && node->kind == LayoutComponentKind::grid);
    title_->setText(node ? node->title : QString{});
    width_->setValue(node ? node->width.value_or(0) : 0);
    action_->setCurrentIndex(action_->findData(
        action_name(node ? node->action.value_or(PlayerAction::count) : PlayerAction::count)
    ));
    orientation_->setCurrentIndex(
        orientation_->findData(node && node->orientation ? static_cast<int>(*node->orientation) : 0)
    );
    columns_->setValue(node ? node->columns.value_or(1) : 1);
    QStringList weights;
    if (node)
        for (const auto value : node->sizes)
            weights.append(QString::number(value));
    sizes_->setText(weights.join(", "));
    updating_ = false;
}

std::optional<LayoutNode> LayoutPropertyInspector::configuration(const LayoutNode& original)
{
    auto result = original;
    result.title = title_->text();
    result.width =
        width_->value() == 0 ? std::optional<int>{} : std::optional<int>{width_->value()};
    if (result.kind == LayoutComponentKind::button && !result.view)
        result.action = parse_action(action_->currentData().toString());
    if (result.kind == LayoutComponentKind::splitter) {
        const auto orientation = orientation_->currentData().toInt();
        result.orientation = orientation == 0
            ? std::optional<Qt::Orientation>{}
            : std::optional<Qt::Orientation>{static_cast<Qt::Orientation>(orientation)};
        result.sizes.clear();
        if (!sizes_->text().trimmed().isEmpty())
            for (const auto& part : sizes_->text().split(',')) {
                bool valid = false;
                const auto value = part.trimmed().toInt(&valid);
                if (!valid || value < 1 || value > 4096) {
                    error_->setText(
                        "Use positive splitter weights from 1 to 4096, separated by commas"
                    );
                    sizes_->setAccessibleDescription(error_->text());
                    return {};
                }
                result.sizes.push_back(value);
            }
        if (!result.sizes.empty() && result.sizes.size() != result.children.size()) {
            error_->setText("Provide exactly one splitter weight per pane");
            sizes_->setAccessibleDescription(error_->text());
            return {};
        }
    }
    if (result.kind == LayoutComponentKind::grid)
        result.columns = columns_->value();
    error_->clear();
    sizes_->setAccessibleDescription({});
    return result;
}

} // namespace wiola::gui
