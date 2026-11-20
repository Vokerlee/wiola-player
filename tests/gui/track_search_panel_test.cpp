/**
 * @file
 * @brief Multi-field search panel regression tests.
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

#include <gui/track_search_panel.hpp>

#include <QApplication>

#include <gtest/gtest.h>

namespace {

TEST(TrackSearchPanel, EmitsSimultaneousFieldsAndClearsEachIndependently)
{
    wiola::gui::TrackSearchPanel panel;
    QLineEdit* title = panel.findChild<QLineEdit*>("searchTitle");
    QLineEdit* artist = panel.findChild<QLineEdit*>("searchArtist");
    QLineEdit* album = panel.findChild<QLineEdit*>("searchAlbum");
    ASSERT_NE(title, nullptr);
    ASSERT_NE(artist, nullptr);
    ASSERT_NE(album, nullptr);

    std::optional<wiola::library::SearchQuery> query;
    panel.on_changed([&](wiola::library::SearchQuery changed) { query = std::move(changed); });

    title->setText(" My Song ");
    artist->setText(" The Artist ");
    album->setText(" Great Album ");
    ASSERT_TRUE(query.has_value());
    EXPECT_EQ(query->title, "my song");
    EXPECT_EQ(query->artist, "the artist");
    EXPECT_EQ(query->album, "great album");

    panel.findChild<QPushButton*>("clearSearchArtist")->click();
    EXPECT_EQ(query->title, "my song");
    EXPECT_TRUE(query->artist.empty());
    EXPECT_EQ(query->album, "great album");
    EXPECT_EQ(title->text(), " My Song ");
    EXPECT_EQ(album->text(), " Great Album ");

    panel.show_results(3, 10);
    EXPECT_EQ(panel.findChild<QLabel*>("trackSearchResults")->text(), "3 of 10 tracks");
}

TEST(TrackSearchPanel, ResetClearsEveryField)
{
    wiola::gui::TrackSearchPanel panel;
    QLineEdit* title = panel.findChild<QLineEdit*>("searchTitle");
    QLineEdit* artist = panel.findChild<QLineEdit*>("searchArtist");
    QLineEdit* album = panel.findChild<QLineEdit*>("searchAlbum");

    title->setText("Title");
    artist->setText("Artist");
    album->setText("Album");

    std::optional<wiola::library::SearchQuery> query;
    panel.on_changed([&](wiola::library::SearchQuery changed) { query = std::move(changed); });
    panel.findChild<QPushButton*>("resetTrackSearch")->click();

    EXPECT_TRUE(title->text().isEmpty());
    EXPECT_TRUE(artist->text().isEmpty());
    EXPECT_TRUE(album->text().isEmpty());
    ASSERT_TRUE(query.has_value());
    EXPECT_TRUE(query->title.empty());
    EXPECT_TRUE(query->artist.empty());
    EXPECT_TRUE(query->album.empty());
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
