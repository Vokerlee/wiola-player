/**
 * @file
 * @brief Explicit metadata conflict review and resolution controls.
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

#include "metadata_conflict_panel.hpp"

#include <QFormLayout>
#include <QVBoxLayout>

namespace wiola::gui {

MetadataConflictPanel::MetadataConflictPanel(QWidget* parent)
    : QWidget{parent}
{
    setObjectName("metadataConflictPanel");

    description_ = new QLabel{this};
    description_->setObjectName("metadataConflictDescription");
    description_->setWordWrap(true);

    reload_ = new QPushButton{"Reload embedded", this};
    reload_->setObjectName("reloadEmbeddedMetadata");
    reload_->setAccessibleName("Reload embedded metadata and discard application overrides");

    keep_ = new QPushButton{"Keep application values", this};
    keep_->setObjectName("keepApplicationMetadata");
    keep_->setAccessibleName("Keep current application values");

    merge_ = new QPushButton{"Merge edited fields", this};
    merge_->setObjectName("mergeMetadataFields");
    merge_->setAccessibleName("Merge edited fields into current records");

    review_ = new QPushButton{"Review current values", this};
    review_->setObjectName("reviewMetadataConflict");
    review_->setAccessibleName("Review current values before overwrite");

    overwrite_ = new QPushButton{"Overwrite", this};
    overwrite_->setObjectName("overwriteMetadataConflict");
    overwrite_->setAccessibleName("Overwrite current values after review");

    auto* commands = new QFormLayout;
    commands->setRowWrapPolicy(QFormLayout::WrapLongRows);
    commands->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    commands->addRow(reload_, keep_);
    commands->addRow(merge_, review_);
    commands->addRow(overwrite_);

    QVBoxLayout* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(description_);
    layout->addLayout(commands);

    connect(reload_, &QPushButton::clicked, this, [this] {
        if (observer_)
            observer_(MetadataConflictAction::reload);
    });
    connect(keep_, &QPushButton::clicked, this, [this] {
        if (observer_)
            observer_(MetadataConflictAction::keep);
    });
    connect(merge_, &QPushButton::clicked, this, [this] {
        if (observer_)
            observer_(MetadataConflictAction::merge);
    });
    connect(review_, &QPushButton::clicked, this, [this] {
        overwrite_->setEnabled(true);
        description_->setText(description_->text() +
            "\nCurrent values reviewed. Overwrite is enabled.");
        if (observer_)
            observer_(MetadataConflictAction::review);
    });
    connect(overwrite_, &QPushButton::clicked, this, [this] {
        if (observer_)
            observer_(MetadataConflictAction::overwrite);
    });

    hide_conflict();
}

void MetadataConflictPanel::show_conflict(const QString& description)
{
    description_->setText(description);
    overwrite_->setEnabled(false);
    setVisible(true);
}

void MetadataConflictPanel::hide_conflict()
{
    setVisible(false);
}

void MetadataConflictPanel::on_action(Observer observer)
{
    observer_ = std::move(observer);
}

} // namespace wiola::gui
