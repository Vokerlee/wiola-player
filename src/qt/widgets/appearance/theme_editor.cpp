/**
 * @file
 * @brief Editable theme copies with typed colors and contrast validation.
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

#include "theme_editor.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>

namespace wiola::gui {

ThemeEditor::ThemeEditor(const ThemeColors& initial, QWidget* parent)
    : QDialog{parent}
    , colors_{initial}
{
    setObjectName("themeEditor");
    setWindowTitle("Customize theme");

    auto* fields = new QFormLayout;

    for (const auto& metric : theme_style_metrics()) {
        auto* field = new QSpinBox{this};
        field->setObjectName("themeStyle:" + QString::fromLatin1(metric.key));
        field->setRange(metric.minimum, metric.maximum);
        field->setSuffix(QString::fromLatin1(metric.suffix));
        field->setValue(style_.*metric.value);
        field->setAccessibleName(QString::fromLatin1(metric.label));
        fields->addRow(QString::fromLatin1(metric.label), field);
        metrics_.emplace_back(metric.value, field);
        connect(field, &QSpinBox::valueChanged, this, [this, member = metric.value](int value) {
            style_.*member = value;
            refresh_preview();
        });
    }

    for (const auto& descriptor : theme_color_descriptors()) {
        const auto role = descriptor.role;
        const auto label = descriptor.label;
        auto* swatch = new QPushButton{this};
        swatch->setObjectName("themeColor:" + QString::fromLatin1(theme_color_key(role)));
        swatch->setAccessibleName(QString{"Change %1 color"}.arg(label));
        swatches_.emplace(role, swatch);
        fields->addRow(label, swatch);

        connect(swatch, &QPushButton::clicked, this, [this, role] {
            const QColor selected =
                QColorDialog::getColor(theme_color(colors_, role), this, "Choose a theme color");
            if (selected.isValid())
                set_color(role, selected);
        });
    }

    auto* presets = new QComboBox{this};
    presets->setObjectName("themePresetSource");
    presets->setAccessibleName("Preset to customize");

    for (const auto& preset : theme_presets())
        presets->addItem(QString::fromLatin1(preset.label));

    auto* copy = new QPushButton{"Use preset as starting point", this};
    copy->setObjectName("copyThemePreset");

    auto* copies = new QHBoxLayout;
    copies->addWidget(presets);
    copies->addWidget(copy);

    status_ = new QLabel{this};
    status_->setObjectName("themeContrastStatus");
    status_->setWordWrap(true);
    status_->setAccessibleName("Theme contrast validation");

    auto* buttons = new QDialogButtonBox{QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this};
    apply_ = buttons->button(QDialogButtonBox::Apply);
    apply_->setObjectName("applyThemeEdit");

    preview_ = new QGroupBox{"Preview", this};
    preview_->setObjectName("themePreview");

    auto* preview_layout = new QHBoxLayout{preview_};
    preview_layout->addWidget(new QPushButton{"Previous", preview_});

    auto* play = new QPushButton{"Play", preview_};
    play->setProperty("appearanceRole", "primary");
    preview_layout->addWidget(play);

    auto* shuffle = new QPushButton{"Shuffle", preview_};
    shuffle->setCheckable(true);
    shuffle->setChecked(true);
    preview_layout->addWidget(shuffle);

    auto* content = new QWidget{this};
    content->setLayout(fields);

    auto* scroll = new QScrollArea{this};
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);

    auto* layout = new QVBoxLayout{this};
    auto* help = new QLabel{
        "Preview changes here. Apply saves your custom theme; Cancel keeps the current look.",
        this};
    help->setWordWrap(true);
    layout->addWidget(help);
    layout->addLayout(copies);
    layout->addWidget(preview_);
    layout->addWidget(scroll, 1);
    layout->addWidget(status_);
    layout->addWidget(buttons);

    connect(copy, &QPushButton::clicked, this, [this, presets] {
        const int selected = presets->currentIndex();

        if (selected < 0 || static_cast<std::size_t>(selected) >= theme_presets().size())
            return;

        const auto& preset = theme_presets()[selected];
        set_colors(theme_colors(preset.palette));
        set_style(preset.style);
    });

    connect(apply_, &QPushButton::clicked, this, [this] {
        if (!theme_contrast_error(colors_).isEmpty())
            return;
        if (!apply_observer_ || apply_observer_(colors_))
            accept();
        else
            status_->setText("Could not save the theme; your changes were retained");
    });

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    set_colors(initial);

    const auto available = screen()->availableGeometry().size() * 0.9;
    resize(QSize{fontMetrics().horizontalAdvance(QString(52, 'M')), fontMetrics().height() * 36}
            .boundedTo(available));
}

void ThemeEditor::set_color(ThemeColorRole role, const QColor& color)
{
    if (!color.isValid())
        return;

    theme_color(colors_, role) = color;
    update_color(role);
    validate();
}

void ThemeEditor::set_colors(const ThemeColors& colors)
{
    colors_ = colors;

    for (const auto& [role, button] : swatches_)
        update_color(role);

    validate();
}

void ThemeEditor::set_style(const ThemeStyle& style)
{
    if (!valid_theme_style(style))
        return;

    style_ = style;

    for (const auto& [member, field] : metrics_)
        field->setValue(style.*member);

    refresh_preview();
}

void ThemeEditor::on_apply(ApplyObserver observer)
{
    apply_observer_ = std::move(observer);
}

void ThemeEditor::update_color(ThemeColorRole role)
{
    QPushButton* swatch = swatches_.at(role);
    const QColor color = theme_color(colors_, role);
    swatch->setText(color.name().toUpper());
    swatch->setStyleSheet(QString{"background-color: %1; color: %2;"}.arg(color.name(),
        contrast_ratio(color, QColor{"#000000"}) >= contrast_ratio(color, QColor{"#ffffff"})
            ? QString{"#000000"}
            : QString{"#ffffff"}));
}

void ThemeEditor::validate()
{
    const QString error = theme_contrast_error(colors_);
    status_->setText(error.isEmpty() ? "Contrast checks passed" : error);
    status_->setProperty("status", error.isEmpty() ? "ready" : "error");
    apply_->setEnabled(error.isEmpty());
    refresh_preview();
}

void ThemeEditor::refresh_preview()
{
    if (!preview_)
        return;

    preview_->setPalette(theme_palette(colors_));
    preview_->setFont(theme_font(style_));
    preview_->setStyleSheet(theme_stylesheet(colors_, style_));
}

} // namespace wiola::gui
