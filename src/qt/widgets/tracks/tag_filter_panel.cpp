/**
 * @file
 * @brief Include/exclude custom tag filters with removable chips.
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

#include "tag_filter_panel.hpp"

#include <wiola/library/tags.hpp>

#include <QComboBox>
#include <QCompleter>
#include <QFormLayout>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStringListModel>
#include <QVBoxLayout>

#include <algorithm>

namespace wiola::gui {

namespace {

constexpr std::size_t max_filters{32};

QString qt(const std::string& text)
{
    return QString::fromStdString(text);
}

} // namespace

TagFilterPanel::TagFilterPanel(QWidget* parent)
    : QWidget{parent}
{
    setObjectName("tagFilterPanel");

    keys_ = new QComboBox{this};
    keys_->setEditable(true);
    keys_->setInsertPolicy(QComboBox::NoInsert);
    keys_->setToolTip("Choose a tag used by your tracks, or enter a tag name");
    key_ = keys_->lineEdit();
    key_->setObjectName("tagFilterKey");
    key_->setAccessibleName("Tag filter key");
    key_->setPlaceholderText("For example: mood");

    value_ = new QLineEdit{this};
    value_->setObjectName("tagFilterValue");
    value_->setAccessibleName("Tag filter value");
    value_->setPlaceholderText("Any value, or enter a value such as calm");

    include_ = new QPushButton{"Has this tag", this};
    include_->setObjectName("includeTagFilter");
    include_->setAccessibleName("Add include tag filter");

    exclude_ = new QPushButton{"Without this tag", this};
    exclude_->setObjectName("excludeTagFilter");
    exclude_->setAccessibleName("Add exclude tag filter");

    disclosure_ = new QToolButton{this};
    disclosure_->setObjectName("tagFilterDisclosure");
    disclosure_->setText("Filter by tags");
    disclosure_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    disclosure_->setCheckable(true);
    disclosure_->setArrowType(Qt::RightArrow);
    editor_ = new QWidget{this};
    auto* fields = new QFormLayout{editor_};
    fields->setContentsMargins(0, 0, 0, 0);
    fields->addRow("Tag name", keys_);
    fields->addRow("Value", value_);
    auto* commands = new QHBoxLayout;
    commands->addWidget(include_);
    commands->addWidget(exclude_);
    commands->addStretch();
    fields->addRow(commands);
    editor_->hide();
    connect(disclosure_, &QToolButton::toggled, this, [this](bool expanded) {
        editor_->setVisible(expanded);
        disclosure_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    });
    auto* chip_content = new QWidget{this};
    chips_ = new QHBoxLayout{chip_content};
    chips_->setContentsMargins(0, 0, 0, 0);
    chips_->addStretch();
    chip_scroll_ = new QScrollArea{this};
    chip_scroll_->setWidgetResizable(true);
    chip_scroll_->setFrameShape(QFrame::NoFrame);
    chip_scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chip_scroll_->setWidget(chip_content);
    chip_scroll_->setFixedHeight(include_->sizeHint().height() +
        chip_scroll_->horizontalScrollBar()->sizeHint().height());
    chip_scroll_->hide();
    auto* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(disclosure_);
    layout->addWidget(chip_scroll_);
    layout->addWidget(editor_);

    connect(include_, &QPushButton::clicked, this,
        [this] { add(library::TagFilterMode::include); });
    connect(exclude_, &QPushButton::clicked, this,
        [this] { add(library::TagFilterMode::exclude); });
}

void TagFilterPanel::suggest_keys(const std::vector<std::string>& keys)
{
    if (keys == available_keys_)
        return;
    available_keys_ = keys;
    const QSignalBlocker block{keys_};
    const auto current = keys_->currentText();
    keys_->clear();
    for (const auto& key : keys)
        keys_->addItem(QString::fromStdString(key));
    keys_->setEditText(current);
    keys_->completer()->setCaseSensitivity(Qt::CaseInsensitive);
}

void TagFilterPanel::add(library::TagFilterMode mode)
{
    const auto clean_key = library::make_tag_value(key_->text().toStdString(), "value");
    if (!clean_key || shown_.size() >= max_filters)
        return;

    const QString raw_value = value_->text();
    std::string clean_value;

    if (!raw_value.isEmpty()) {
        const auto valid = library::make_tag_value("key", raw_value.toStdString());
        if (!valid)
            return;

        clean_value = valid->value;
    }

    library::TagFilter filter{.key = clean_key->key, .value = clean_value, .mode = mode};

    key_->clear();
    value_->clear();
    if (observer_)
        observer_({TagFilterEdit::Kind::add, std::move(filter)});
}

void TagFilterPanel::show_filters(std::vector<library::TagFilter> filters)
{
    if (filters == shown_)
        return;
    shown_ = std::move(filters);
    rebuild_chips();
}

void TagFilterPanel::rebuild_chips()
{
    for (QPushButton* button : chip_buttons_) {
        chips_->removeWidget(button);
        button->deleteLater();
    }

    chip_buttons_.clear();
    chip_scroll_->setVisible(!shown_.empty());

    for (std::size_t index = 0; index < shown_.size(); ++index) {
        const library::TagFilter& filter = shown_[index];
        const QString label =
            (filter.mode == library::TagFilterMode::include ? "Has " : "Without ") +
            qt(filter.key) + (filter.value.empty() ? QString{} : ": " + qt(filter.value));
        QPushButton* chip = new QPushButton{label + " ×", this};
        chip->setObjectName("tagFilterChip");
        chip->setToolTip("Remove this filter");
        chip->setAccessibleName("Remove filter " + label);
        chips_->insertWidget(static_cast<int>(chip_buttons_.size()), chip);
        chip_buttons_.push_back(chip);

        connect(chip, &QPushButton::clicked, this, [this, filter] {
            if (observer_)
                observer_({TagFilterEdit::Kind::remove, filter});
        });
    }
}

void TagFilterPanel::on_edit(Observer observer)
{
    observer_ = std::move(observer);
}

} // namespace wiola::gui
