/**
 * @file
 * @brief A length of time, written the way a listener reads it.
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

#include "volume_view.hpp"
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <qt/widgets/appearance/presentation_tokens.hpp>
#include <qt/widgets/layout/view_registry.hpp>
#include <stdexcept>

namespace wiola::gui {

VolumeView::VolumeView(QWidget* parent)
    : QWidget{parent}
{
    const auto tokens = PresentationTokens{}.player;
    auto* body = new QHBoxLayout{this};
    body->setContentsMargins(0, 0, 0, 0);
    slider_ = new QSlider{Qt::Horizontal, this};
    slider_->setToolTip("Volume");
    slider_->setAccessibleName("Volume");
    slider_->setMinimumWidth(tokens.volume_slider_width);
    value_ = new QLabel{this};
    value_->setMinimumWidth(fontMetrics().horizontalAdvance("300%"));
    boost_ = new QPushButton{"+", this};
    boost_->setCheckable(true);
    boost_->setFocusPolicy(Qt::StrongFocus);
    boost_->setAccessibleName("Boost volume above 100%");
    boost_->setToolTip("Allow volume above 100%");
    body->addWidget(slider_);
    body->addWidget(value_);
    body->addWidget(boost_);
    connect(slider_, &QSlider::valueChanged, this, &VolumeView::volume_requested);
    connect(boost_, &QPushButton::toggled, this, &VolumeView::boost_requested);
}

void VolumeView::present(VolumePresentation state)
{
    const QSignalBlocker slider{slider_}, boost{boost_};
    slider_->setRange(0, state.maximum);
    slider_->setValue(state.position);
    boost_->setChecked(state.boosted);
    value_->setText(QString::number(state.position) + "%");
}

void VolumeView::register_views(ViewRegistry& registry)
{
    const auto add = [&](const char* key, QWidget* widget, LayoutComponentKind kind,
                         bool structural = false) {
        if (!registry.add(component_view(key), widget, kind, structural))
            throw std::logic_error{"duplicate volume view"};
    };
    add("volumeSection", this, LayoutComponentKind::row, true);
    add("volumeSlider", slider_, LayoutComponentKind::volume);
    add("volumeValue", value_, LayoutComponentKind::status);
    add("boostButton", boost_, LayoutComponentKind::button);
}

} // namespace wiola::gui
