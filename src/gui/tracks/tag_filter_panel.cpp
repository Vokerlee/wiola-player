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

#include <library/tags.hpp>

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

    key_ = new QLineEdit{this};
    key_->setObjectName("tagFilterKey");
    key_->setAccessibleName("Tag filter key");
    key_->setPlaceholderText("Tag key");

    value_ = new QLineEdit{this};
    value_->setObjectName("tagFilterValue");
    value_->setAccessibleName("Tag filter value");
    value_->setPlaceholderText("Value (empty means any)");

    include_ = new QPushButton{"Include", this};
    include_->setObjectName("includeTagFilter");
    include_->setAccessibleName("Add include tag filter");

    exclude_ = new QPushButton{"Exclude", this};
    exclude_->setObjectName("excludeTagFilter");
    exclude_->setAccessibleName("Add exclude tag filter");

    QHBoxLayout* editor = new QHBoxLayout;
    editor->addWidget(key_);
    editor->addWidget(value_);
    editor->addWidget(include_);
    editor->addWidget(exclude_);

    chips_ = new QHBoxLayout;
    chips_->addStretch();

    QVBoxLayout* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(editor);
    layout->addLayout(chips_);

    connect(include_, &QPushButton::clicked, this,
        [this] { add(library::TagFilterMode::include); });
    connect(exclude_, &QPushButton::clicked, this,
        [this] { add(library::TagFilterMode::exclude); });
}

void TagFilterPanel::add(library::TagFilterMode mode)
{
    const auto clean_key = library::make_tag_value(key_->text().toStdString(), "value");
    if (!clean_key || filters_.size() >= max_filters)
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

    const bool duplicate = std::ranges::any_of(filters_, [&](const library::TagFilter& present) {
        return present.mode == filter.mode &&
            library::normalized_tag_text(present.key) == library::normalized_tag_text(filter.key) &&
            library::normalized_tag_text(present.value) ==
            library::normalized_tag_text(filter.value);
    });

    if (duplicate)
        return;

    filters_.push_back(std::move(filter));
    key_->clear();
    value_->clear();
    rebuild_chips();

    if (observer_)
        observer_(filters_);
}

void TagFilterPanel::rebuild_chips()
{
    for (QPushButton* button : chip_buttons_) {
        chips_->removeWidget(button);
        button->deleteLater();
    }

    chip_buttons_.clear();

    for (std::size_t index = 0; index < filters_.size(); ++index) {
        const library::TagFilter& filter = filters_[index];
        const QString label = (filter.mode == library::TagFilterMode::include ? "+ " : "- ") +
            qt(filter.key) + (filter.value.empty() ? QString{} : ": " + qt(filter.value));
        QPushButton* chip = new QPushButton{label, this};
        chip->setObjectName("tagFilterChip");
        chip->setAccessibleName("Remove filter " + label);
        chips_->insertWidget(static_cast<int>(chip_buttons_.size()), chip);
        chip_buttons_.push_back(chip);

        connect(chip, &QPushButton::clicked, this, [this, index] {
            filters_.erase(filters_.begin() + static_cast<std::ptrdiff_t>(index));
            rebuild_chips();

            if (observer_)
                observer_(filters_);
        });
    }
}

void TagFilterPanel::on_changed(Observer observer)
{
    observer_ = std::move(observer);
}

} // namespace wiola::gui
