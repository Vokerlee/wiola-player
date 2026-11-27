/**
 * @file
 * @brief The window a listener shapes the sound from.
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

#include "equalizer_panel.hpp"

#include <qt/widgets/appearance/presentation_tokens.hpp>
#include <wiola/app/playback/equalizer_control.hpp>
#include <wiola/units.hpp>

#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace wiola::gui {

namespace {

constexpr PresentationTokens tokens;

/// A center as a band is labelled: whole hertz up to a thousand, then thousands.
QString as_label(units::Frequency center)
{
    const double hz{center.get<units::Hz>()};

    if (hz < 1000.0)
        return QString::number(qRound(hz));

    return QString{"%1k"}.arg(qRound(hz / 1000.0));
}

/// A slider position as the gain it stands for.
constexpr double as_db(int steps)
{
    return static_cast<double>(steps) / tokens.equalizer.gain_steps_per_db;
}

/// A gain as it is read off the panel: signed, and to a tenth of a decibel.
QString as_gain(int steps)
{
    const double db{as_db(steps)};

    return (steps > 0 ? QString{"+"} : QString{}) + QString::number(db, 'f', 1);
}

/// The slider position a gain in decibels stands on.
int as_steps(float db)
{
    return static_cast<int>(std::lround(db * tokens.equalizer.gain_steps_per_db));
}

int band_limit(const app::EqualizerControl& equalizer) noexcept
{
    return static_cast<int>(equalizer.max_band_gain()) * tokens.equalizer.gain_steps_per_db;
}

} // namespace

EqualizerPanel::EqualizerPanel(app::EqualizerControl& equalizer, QWidget* parent)
    : QWidget{parent, Qt::Window}
    , equalizer_{equalizer}
{
    setWindowTitle("Equalizer");

    enabled_box_ = new QCheckBox{"Equalizer", this};
    enabled_box_->setAccessibleName("Enable equalizer");
    enabled_box_->setChecked(equalizer_.enabled());

    bands_row_ = new QHBoxLayout;

    auto* layout = new QVBoxLayout{this};
    layout->addWidget(enabled_box_);

    auto* scroll = new QScrollArea{this};
    scroll->setObjectName("equalizerBandsScroll");
    scroll->setWidgetResizable(true);

    auto* contents = new QWidget;
    contents->setLayout(bands_row_);
    scroll->setWidget(contents);
    layout->addWidget(scroll);

    connect(enabled_box_, &QCheckBox::toggled, this,
        [this](bool on) { equalizer_.set_enabled(on); });

    rebuild_bands();
}

void EqualizerPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    rebuild_bands();
}

void EqualizerPanel::rebuild_bands()
{
    delete bands_;

    bands_ = new QWidget{this};
    bands_row_->addWidget(bands_);

    auto* row = new QHBoxLayout{bands_};
    const int limit{band_limit(equalizer_)};

    for (std::size_t i = 0; i < equalizer_.num_bands(); ++i) {
        auto* slider = new QSlider{Qt::Vertical, bands_};

        slider->setRange(-limit, limit);
        slider->setSingleStep(tokens.equalizer.gain_step);
        slider->setValue(as_steps(equalizer_.band_gain(i)));
        slider->setMinimumHeight(std::max(tokens.equalizer.band_slider_height,
            fontMetrics().height() * 6));
        slider->setAccessibleName(as_label(equalizer_.band_center(i)) + " equalizer gain");

        auto* gain = new QLabel{as_gain(slider->value()), bands_};
        gain->setMinimumWidth(fontMetrics().horizontalAdvance("-12.0 dB"));
        gain->setAlignment(Qt::AlignHCenter);

        auto* center = new QLabel{as_label(equalizer_.band_center(i)), bands_};
        center->setMinimumWidth(fontMetrics().horizontalAdvance(center->text()));
        center->setAlignment(Qt::AlignHCenter);

        connect(slider, &QSlider::valueChanged, this, [this, i, gain](int steps) {
            equalizer_.set_band_gain(i, static_cast<float>(as_db(steps)));
            gain->setText(as_gain(steps));
        });

        auto* band = new QVBoxLayout;
        band->addWidget(gain);
        band->addWidget(slider, 0, Qt::AlignHCenter);
        band->addWidget(center);

        row->addLayout(band);
    }
}

} // namespace wiola::gui
