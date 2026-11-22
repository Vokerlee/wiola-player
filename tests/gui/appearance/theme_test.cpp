/**
 * @file
 * @brief Native semantic theme regression tests.
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

#include <gui/appearance/theme.hpp>

#include <QApplication>
#include <QWidget>

#include <gtest/gtest.h>

namespace {

TEST(Theme, LightAndDarkPalettesDefineSemanticStates)
{
    const auto light = wiola::gui::theme_colors(wiola::gui::Theme::light);
    const auto dark = wiola::gui::theme_colors(wiola::gui::Theme::dark);
    EXPECT_NE(light.window, dark.window);
    EXPECT_NE(light.text, dark.text);

    for (const wiola::gui::Theme theme : {wiola::gui::Theme::light, wiola::gui::Theme::dark}) {
        const auto colors = wiola::gui::theme_colors(theme);
        const QPalette palette = wiola::gui::theme_palette(colors);

        EXPECT_EQ(palette.color(QPalette::Active, QPalette::Window), colors.window);
        EXPECT_EQ(palette.color(QPalette::Active, QPalette::Base), colors.surface);
        EXPECT_EQ(palette.color(QPalette::Active, QPalette::Highlight), colors.accent);
        EXPECT_EQ(palette.color(QPalette::Active, QPalette::HighlightedText), colors.accent_text);
        EXPECT_EQ(palette.color(QPalette::Disabled, QPalette::Text), colors.disabled_text);
        EXPECT_EQ(palette.color(QPalette::Disabled, QPalette::WindowText), colors.disabled_text);
        EXPECT_NE(palette.color(QPalette::Active, QPalette::Text),
            palette.color(QPalette::Disabled, QPalette::Text));
    }
}

TEST(Theme, ApplyingDarkThemeUpdatesNativePaletteAndSemanticStates)
{
    wiola::gui::apply_theme(wiola::gui::Theme::dark);
    const auto colors = wiola::gui::theme_colors(wiola::gui::Theme::dark);

    EXPECT_EQ(QApplication::palette().color(QPalette::Window), colors.window);
    EXPECT_TRUE(qApp->styleSheet().contains(colors.focus.name()));
    EXPECT_TRUE(qApp->styleSheet().contains(colors.error.name()));
    EXPECT_TRUE(qApp->styleSheet().contains("padding: 5px"));
    EXPECT_TRUE(qApp->styleSheet().contains("QLabel[status=\"error\"]"));
    EXPECT_TRUE(qApp->styleSheet().contains("QLabel[status=\"busy\"]"));
}

TEST(Theme, CustomIconsAndArtworkFollowTheActivePalette)
{
    wiola::gui::apply_theme(wiola::gui::Theme::light);
    QWidget light;
    const QImage light_icon =
        wiola::gui::themed_standard_icon(light, QStyle::SP_MediaPlay).pixmap(24, 24).toImage();
    const QImage light_art = wiola::gui::themed_artwork_placeholder(light, 96).toImage();

    wiola::gui::apply_theme(wiola::gui::Theme::dark);
    QWidget dark;
    const QIcon dark_themed_icon = wiola::gui::themed_standard_icon(dark, QStyle::SP_MediaPlay);
    const QImage dark_icon = dark_themed_icon.pixmap(24, 24).toImage();
    const QImage dark_art = wiola::gui::themed_artwork_placeholder(dark, 96).toImage();

    EXPECT_NE(light_icon, dark_icon);
    EXPECT_NE(light_art, dark_art);
    EXPECT_FALSE(dark_themed_icon.pixmap(24, 24, QIcon::Disabled).isNull());
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
