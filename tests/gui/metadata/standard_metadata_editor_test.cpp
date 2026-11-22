/**
 * @file
 * @brief Standard metadata editor dirty-state and validation tests.
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

#include <gui/metadata/standard_metadata_editor.hpp>

#include <QApplication>

#include <gtest/gtest.h>

namespace {

class StandardMetadataEditor : public testing::Test {
protected:
    void SetUp() override
    {
        track.id = wiola::library::MediaId{"media"};
        track.metadata_revision = 1;
        track.embedded.title = "Embedded title";
        track.embedded.artist = "Embedded artist";
        track.embedded.album = "Embedded album";
        track.embedded.genre = "Rock";
        track.embedded.year = 2020;
        track.embedded.comment = "Embedded comment";
        track.embedded.composer = "Embedded composer";
        track.embedded.track_number = 1;
        track.embedded.disc_number = 1;
        track.overrides.title = "User title";
        editor.show_track(&track);
    }

    QLineEdit* field(const char* name) { return editor.findChild<QLineEdit*>(name); }

    QPushButton* button(const char* name) { return editor.findChild<QPushButton*>(name); }

    wiola::library::TrackRecord track;
    wiola::gui::StandardMetadataEditor editor;
};

TEST_F(StandardMetadataEditor, ShowsResolvedValuesAndAppliesDirtyOverrides)
{
    EXPECT_EQ(field("standardTitle")->text(), "User title");
    EXPECT_EQ(field("standardArtist")->text(), "Embedded artist");
    EXPECT_FALSE(button("applyStandardMetadata")->isEnabled());
    EXPECT_FALSE(button("cancelStandardMetadata")->isEnabled());
    EXPECT_TRUE(editor.findChild<QLabel*>("standardMetadataStatus")
            ->text()
            .contains("Application overrides"));

    std::optional<wiola::library::StandardFieldOverrides> applied;
    editor.on_apply([&](wiola::library::MediaId media,
                        wiola::library::StandardFieldOverrides edit) {
        EXPECT_EQ(media, track.id);
        applied = std::move(edit);
    });

    field("standardGenre")->setText("Jazz");
    field("standardYear")->setText("2026");
    field("standardTrackNumber")->setText("4");
    field("standardDiscNumber")->clear();
    EXPECT_TRUE(button("applyStandardMetadata")->isEnabled());
    button("applyStandardMetadata")->click();

    ASSERT_TRUE(applied.has_value());
    EXPECT_EQ(applied->genre, "Jazz");
    EXPECT_EQ(applied->year, 2026);
    EXPECT_EQ(applied->track_number, 4);
    ASSERT_TRUE(applied->disc_number.has_value());
    EXPECT_FALSE(applied->disc_number->has_value());
}

TEST_F(StandardMetadataEditor, RejectsInvalidNumbersAndCancelsCleanly)
{
    field("standardYear")->setText("not-a-year");
    EXPECT_TRUE(editor.findChild<QLabel*>("standardMetadataStatus")
            ->text()
            .contains("non-negative integers"));
    EXPECT_FALSE(button("applyStandardMetadata")->isEnabled());
    EXPECT_TRUE(button("cancelStandardMetadata")->isEnabled());

    button("cancelStandardMetadata")->click();
    EXPECT_EQ(field("standardYear")->text(), "2020");
    EXPECT_EQ(field("standardTitle")->text(), "User title");
    EXPECT_FALSE(button("applyStandardMetadata")->isEnabled());
}

TEST_F(StandardMetadataEditor, PreservesDirtyInputUntilARevisionChanges)
{
    field("standardComment")->setText("Unsaved");
    editor.show_track(&track);
    EXPECT_EQ(field("standardComment")->text(), "Unsaved");

    ++track.metadata_revision;
    editor.show_track(&track);
    EXPECT_EQ(field("standardComment")->text(), "Embedded comment");

    editor.show_status("Could not save");
    EXPECT_EQ(editor.findChild<QLabel*>("standardMetadataStatus")->text(), "Could not save");
}

TEST_F(StandardMetadataEditor, MixedValuesRemainUnchangedUnlessExplicitlyEdited)
{
    wiola::library::TrackRecord other = track;
    other.id = wiola::library::MediaId{"other"};
    other.embedded.artist = "Different artist";
    other.embedded.year = 2021;
    editor.show_tracks({&track, &other});

    EXPECT_EQ(field("standardTitle")->text(), "User title");
    EXPECT_TRUE(field("standardArtist")->text().isEmpty());
    EXPECT_EQ(field("standardArtist")->placeholderText(), "Mixed values");
    EXPECT_TRUE(field("standardYear")->text().isEmpty());
    EXPECT_EQ(field("standardYear")->placeholderText(), "Mixed values");

    std::optional<wiola::library::StandardFieldOverrides> applied;
    editor.on_apply([&](wiola::library::MediaId, wiola::library::StandardFieldOverrides edit) {
        applied = std::move(edit);
    });
    field("standardGenre")->setText("Jazz");
    button("applyStandardMetadata")->click();

    ASSERT_TRUE(applied.has_value());
    EXPECT_EQ(applied->genre, "Jazz");
    EXPECT_FALSE(applied->artist.has_value());
    EXPECT_FALSE(applied->year.has_value());
    EXPECT_FALSE(applied->title.has_value());
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
