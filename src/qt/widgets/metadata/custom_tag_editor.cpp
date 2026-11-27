/**
 * @file
 * @brief Accessible editor for shared custom tag values.
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

#include "custom_tag_editor.hpp"
#include <wiola/library/tags.hpp>

#include <QFormLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace wiola::gui {

namespace {

constexpr int tag_id_role = Qt::UserRole;
constexpr int tag_key_role = Qt::UserRole + 1;
constexpr int tag_value_role = Qt::UserRole + 2;

QString qt(const std::string& text)
{
    return QString::fromStdString(text);
}

} // namespace

CustomTagEditor::CustomTagEditor(QWidget* parent)
    : QWidget{parent}
{
    setObjectName("customTagEditor");

    explanation_ = new QLabel{this};
    explanation_->setObjectName("customTagExplanation");
    explanation_->setWordWrap(true);

    values_ = new QListWidget{this};
    values_->setObjectName("customTagValues");
    values_->setAccessibleName("Shared custom tag values");
    values_->setSelectionMode(QAbstractItemView::SingleSelection);
    values_->setMaximumHeight(140);

    key_ = new QLineEdit{this};
    key_->setObjectName("customTagKey");
    key_->setAccessibleName("Custom tag key");
    key_->setPlaceholderText("Key, for example genre");
    key_->setMaxLength(static_cast<int>(library::max_tag_key_bytes));

    value_ = new QLineEdit{this};
    value_->setObjectName("customTagValue");
    value_->setAccessibleName("Custom tag value");
    value_->setPlaceholderText("Value");
    value_->setMaxLength(static_cast<int>(library::max_tag_value_bytes));

    add_ = new QPushButton{"Add", this};
    add_->setObjectName("addCustomTag");
    add_->setAccessibleName("Add custom tag");

    edit_ = new QPushButton{"Edit", this};
    edit_->setObjectName("editCustomTag");
    edit_->setAccessibleName("Edit custom tag");

    remove_ = new QPushButton{"Remove", this};
    remove_->setObjectName("removeCustomTag");
    remove_->setAccessibleName("Remove custom tag");

    auto* fields = new QFormLayout;
    fields->setRowWrapPolicy(QFormLayout::WrapLongRows);
    fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    fields->addRow("Tag", key_);
    fields->addRow("Value", value_);

    auto* commands = new QFormLayout;
    commands->setRowWrapPolicy(QFormLayout::WrapLongRows);
    commands->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    commands->addRow(add_, edit_);
    commands->addRow(remove_);

    QVBoxLayout* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(explanation_);
    layout->addWidget(values_);
    layout->addLayout(fields);
    layout->addLayout(commands);

    connect(values_, &QListWidget::itemSelectionChanged, this, [this] { show_selection(); });

    connect(add_, &QPushButton::clicked, this, [this] {
        if (add_observer_)
            add_observer_(media_, key_->text().toStdString(), value_->text().toStdString());
    });

    connect(edit_, &QPushButton::clicked, this, [this] {
        if (edit_observer_ && selected_tag().valid()) {
            edit_observer_(media_, selected_tag(), key_->text().toStdString(),
                value_->text().toStdString());
        }
    });

    connect(remove_, &QPushButton::clicked, this, [this] {
        if (remove_observer_ && selected_tag().valid())
            remove_observer_(media_, selected_tag());
    });

    show_track(nullptr);
}

void CustomTagEditor::show_track(const library::TrackRecord* track)
{
    const library::MediaId next_media = track != nullptr ? track->id : library::MediaId{};
    const std::uint64_t next_revision = track != nullptr ? track->metadata_revision : 0;

    if (initialized_ && media_ == next_media && revision_ == next_revision)
        return;

    const library::TagId selected = media_ == next_media ? selected_tag() : library::TagId{};
    media_ = next_media;
    revision_ = next_revision;
    initialized_ = true;

    const QSignalBlocker blocked{values_};
    values_->clear();
    key_->clear();
    value_->clear();

    int selected_row = -1;

    if (track != nullptr) {
        for (const library::TagRecord& tag : track->tags) {
            auto* item = new QListWidgetItem{qt(tag.key) + ": " + qt(tag.value), values_};
            item->setData(tag_id_role, qt(tag.id.value));
            item->setData(tag_key_role, qt(tag.key));
            item->setData(tag_value_role, qt(tag.value));

            if (tag.id == selected)
                selected_row = values_->row(item);
        }
    }

    if (selected_row >= 0)
        values_->setCurrentRow(selected_row);

    explanation_->setText(track != nullptr
            ? QString{"Shared across every playlist. Embedded metadata is not changed."}
            : QString{"Select a track to edit shared custom tags."});

    update_actions();
    show_selection();
}

library::TagId CustomTagEditor::selected_tag() const
{
    const QListWidgetItem* selected = values_->currentItem();
    return selected != nullptr
        ? library::TagId{selected->data(tag_id_role).toString().toStdString()}
        : library::TagId{};
}

void CustomTagEditor::show_selection()
{
    const QListWidgetItem* selected = values_->currentItem();
    key_->setText(selected != nullptr ? selected->data(tag_key_role).toString() : QString{});
    value_->setText(selected != nullptr ? selected->data(tag_value_role).toString() : QString{});
    update_actions();
}

void CustomTagEditor::update_actions()
{
    const bool has_track = media_.valid();
    const bool has_selection = selected_tag().valid();

    values_->setEnabled(has_track);
    key_->setEnabled(has_track);
    value_->setEnabled(has_track);
    add_->setEnabled(has_track);
    edit_->setEnabled(has_track && has_selection);
    remove_->setEnabled(has_track && has_selection);
}

void CustomTagEditor::on_add(AddObserver observer)
{
    add_observer_ = std::move(observer);
}

void CustomTagEditor::on_edit(EditObserver observer)
{
    edit_observer_ = std::move(observer);
}

void CustomTagEditor::on_remove(RemoveObserver observer)
{
    remove_observer_ = std::move(observer);
}

} // namespace wiola::gui
