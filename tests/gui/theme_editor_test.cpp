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

#include <gui/theme_editor.hpp>

#include <QApplication>
#include <QLabel>

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
    editor.on_apply([&](const wiola::gui::ThemeColors&) { ++applications; });
    apply->click();
    EXPECT_EQ(applications, 0);

    editor.findChild<QPushButton*>("copyLightTheme")->click();
    ASSERT_TRUE(apply->isEnabled());
    EXPECT_EQ(editor.colors().window, wiola::gui::theme_colors(wiola::gui::Theme::light).window);

    editor.set_color(wiola::gui::ThemeColorRole::window, QColor{"#f0f0f0"});
    apply->click();
    EXPECT_EQ(applications, 1);
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
