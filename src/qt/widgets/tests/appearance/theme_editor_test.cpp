/**
 * @file
 * @brief Editable theme and contrast validation regression tests.
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

#include <qt/widgets/appearance/theme_editor.hpp>

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>

#include <gtest/gtest.h>

namespace {

TEST(ThemeContrast, ValidatesBuiltinsAndCalculatesAccessibleRatios)
{
    EXPECT_NEAR(wiola::gui::contrast_ratio(QColor{"#000000"}, QColor{"#ffffff"}), 21.0, 0.01);
    EXPECT_TRUE(wiola::gui::theme_contrast_error(wiola::gui::theme_colors(wiola::gui::Theme::light))
            .isEmpty());
    EXPECT_TRUE(wiola::gui::theme_contrast_error(wiola::gui::theme_colors(wiola::gui::Theme::dark))
            .isEmpty());
}

TEST(ThemeEditor, RejectsLowContrastAndAppliesAnEditableCopy)
{
    wiola::gui::ThemeEditor editor{wiola::gui::theme_colors(wiola::gui::Theme::dark)};
    auto* apply = editor.findChild<QPushButton*>("applyThemeEdit");
    auto* status = editor.findChild<QLabel*>("themeContrastStatus");
    ASSERT_NE(apply, nullptr);
    ASSERT_NE(status, nullptr);
    EXPECT_TRUE(apply->isEnabled());

    editor.set_color(wiola::gui::ThemeColorRole::text, editor.colors().surface);
    EXPECT_FALSE(apply->isEnabled());
    EXPECT_TRUE(status->text().contains("4.5:1"));

    int applications = 0;
    editor.on_apply([&](const wiola::gui::ThemeColors&) {
        ++applications;
        return true;
    });
    apply->click();
    EXPECT_EQ(applications, 0);

    editor.findChild<QComboBox*>("themePresetSource")->setCurrentIndex(0);
    editor.findChild<QPushButton*>("copyThemePreset")->click();
    ASSERT_TRUE(apply->isEnabled());
    EXPECT_EQ(editor.colors().window, wiola::gui::theme_colors(wiola::gui::Theme::light).window);

    editor.set_color(wiola::gui::ThemeColorRole::window, QColor{"#f0f0f0"});
    apply->click();
    EXPECT_EQ(applications, 1);
    EXPECT_EQ(editor.result(), QDialog::Accepted);
}

TEST(ThemeEditor, PresetsCopyShapeAndColorsAndMetricsPreviewRemainsLocalUntilApply)
{
    using namespace wiola::gui;
    const auto global_sheet = qApp->styleSheet();
    const auto global_palette = QApplication::palette();
    ThemeEditor editor{theme_colors(Theme::light)};
    auto* presets = editor.findChild<QComboBox*>("themePresetSource");
    auto* copy = editor.findChild<QPushButton*>("copyThemePreset");
    auto* preview = editor.findChild<QWidget*>("themePreview");
    ASSERT_NE(presets, nullptr);
    ASSERT_NE(copy, nullptr);
    ASSERT_NE(preview, nullptr);
    for (std::size_t index = 0; index < theme_presets().size(); ++index) {
        presets->setCurrentIndex(static_cast<int>(index));
        copy->click();
        const auto& preset = theme_presets()[index];
        EXPECT_EQ(editor.colors(), theme_colors(preset.palette));
        EXPECT_EQ(editor.style(), preset.style);
    }
    const ThemeStyle personal{5, 9, 115};
    for (const auto& metric : theme_style_metrics()) {
        auto* field = editor.findChild<QSpinBox*>("themeStyle:" + QString::fromLatin1(metric.key));
        ASSERT_NE(field, nullptr);
        field->setValue(personal.*metric.value);
    }
    EXPECT_EQ(editor.style(), personal);
    EXPECT_TRUE(preview->styleSheet().contains("border-radius: 5px"));
    EXPECT_EQ(qApp->styleSheet(), global_sheet);
    EXPECT_EQ(QApplication::palette(), global_palette);
    bool applied{};
    editor.on_apply([&](const auto&) {
        applied = true;
        EXPECT_EQ(editor.style(), personal);
        return true;
    });
    editor.findChild<QPushButton*>("applyThemeEdit")->click();
    EXPECT_TRUE(applied);
}

TEST(ThemeEditor, FailedDurableAcceptanceRetainsTheDraftForRetry)
{
    wiola::gui::ThemeEditor editor{wiola::gui::theme_colors(wiola::gui::Theme::light)};
    editor.set_color(wiola::gui::ThemeColorRole::window, QColor{"#f0f0f0"});
    editor.on_apply([](const auto&) { return false; });
    auto* apply = editor.findChild<QPushButton*>("applyThemeEdit");
    ASSERT_NE(apply, nullptr);
    apply->click();
    EXPECT_NE(editor.result(), QDialog::Accepted);
    EXPECT_EQ(editor.colors().window, QColor{"#f0f0f0"});
    EXPECT_TRUE(editor.findChild<QLabel*>("themeContrastStatus")->text().contains("retained"));
    editor.on_apply([](const auto&) { return true; });
    apply->click();
    EXPECT_EQ(editor.result(), QDialog::Accepted);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
