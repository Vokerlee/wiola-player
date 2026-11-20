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
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <array>

namespace wiola::gui {

ThemeEditor::ThemeEditor(const ThemeColors& initial, QWidget* parent)
    : QDialog{parent}
    , colors_{initial}
{
    setObjectName("themeEditor");
    setWindowTitle("Edit theme");

    auto* fields = new QFormLayout;
    const std::array fields_to_show{
        std::pair{ThemeColorRole::window,        "Window"        },
        std::pair{ThemeColorRole::surface,       "Surface"       },
        std::pair{ThemeColorRole::text,          "Text"          },
        std::pair{ThemeColorRole::muted_text,    "Secondary text"},
        std::pair{ThemeColorRole::disabled_text, "Disabled text" },
        std::pair{ThemeColorRole::accent,        "Selection"     },
        std::pair{ThemeColorRole::accent_text,   "Selected text" },
        std::pair{ThemeColorRole::focus,         "Keyboard focus"},
        std::pair{ThemeColorRole::error,         "Error"         },
    };

    for (const auto& [role, label] : fields_to_show) {
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

    auto* light = new QPushButton{"Copy light theme", this};
    light->setObjectName("copyLightTheme");
    auto* dark = new QPushButton{"Copy dark theme", this};
    dark->setObjectName("copyDarkTheme");
    auto* copies = new QHBoxLayout;
    copies->addWidget(light);
    copies->addWidget(dark);

    status_ = new QLabel{this};
    status_->setObjectName("themeContrastStatus");
    status_->setWordWrap(true);
    status_->setAccessibleName("Theme contrast validation");

    auto* buttons = new QDialogButtonBox{QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this};
    apply_ = buttons->button(QDialogButtonBox::Apply);
    apply_->setObjectName("applyThemeEdit");

    auto* layout = new QVBoxLayout{this};
    layout->addLayout(copies);
    layout->addLayout(fields);
    layout->addWidget(status_);
    layout->addWidget(buttons);

    connect(light, &QPushButton::clicked, this, [this] { set_colors(theme_colors(Theme::light)); });
    connect(dark, &QPushButton::clicked, this, [this] { set_colors(theme_colors(Theme::dark)); });
    connect(apply_, &QPushButton::clicked, this, [this] {
        if (apply_observer_)
            apply_observer_(colors_);
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    set_colors(initial);
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
}

} // namespace wiola::gui
