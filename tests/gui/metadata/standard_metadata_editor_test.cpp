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

#include <app/metadata/metadata_edit_service.hpp>
#include <gui/metadata/standard_metadata_editor.hpp>

#include <QApplication>

#include <algorithm>
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

    std::optional<wiola::library::StandardMetadataPatch> applied;
    editor.on_apply([&](const wiola::app::MetadataEditSession& session) {
        EXPECT_EQ(session.media(), std::vector<wiola::library::MediaId>{track.id});
        applied = session.draft();
    });

    field("standardGenre")->setText("Jazz");
    field("standardYear")->setText("2026");
    field("standardTrackNumber")->setText("4");
    field("standardDiscNumber")->clear();
    EXPECT_TRUE(button("applyStandardMetadata")->isEnabled());
    button("applyStandardMetadata")->click();

    ASSERT_TRUE(applied.has_value());
    EXPECT_EQ(*applied->genre.value(), "Jazz");
    EXPECT_EQ(*applied->year.value(), 2026);
    EXPECT_EQ(*applied->track_number.value(), 4);
    EXPECT_EQ(applied->disc_number.operation(), wiola::library::FieldEditOperation::clear);
}

TEST_F(StandardMetadataEditor, NumericBoundariesRejectOverflowAndKeepUntouchedFieldsUnchanged)
{
    for (const auto* name : {"standardYear", "standardTrackNumber", "standardDiscNumber"}) {
        SCOPED_TRACE(name);
        for (const auto* invalid : {"-1", "4294967296", "1.5"}) {
            field(name)->setText(invalid);
            EXPECT_FALSE(button("applyStandardMetadata")->isEnabled()) << invalid;
            EXPECT_FALSE(field(name)->accessibleDescription().isEmpty());
            button("cancelStandardMetadata")->click();
        }
        std::optional<wiola::library::StandardMetadataPatch> applied;
        editor.on_apply([&](const auto& session) { applied = session.draft(); });
        field(name)->setText("4294967295");
        ASSERT_TRUE(button("applyStandardMetadata")->isEnabled());
        button("applyStandardMetadata")->click();
        ASSERT_TRUE(applied);
        EXPECT_EQ(applied->title.operation(), wiola::library::FieldEditOperation::unchanged);
        const auto* number = std::string_view{name} == "standardYear" ? applied->year.value()
            : std::string_view{name} == "standardTrackNumber" ? applied->track_number.value()
                                                              : applied->disc_number.value();
        ASSERT_NE(number, nullptr);
        EXPECT_EQ(*number, 4294967295U);
        button("cancelStandardMetadata")->click();
    }
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

TEST_F(StandardMetadataEditor, PreservesDraftAndExpectedTargetAcrossRevisionsAndSelectionChanges)
{
    field("standardComment")->setText("Unsaved");
    editor.show_track(&track);
    EXPECT_EQ(field("standardComment")->text(), "Unsaved");

    ++track.metadata_revision;
    editor.show_track(&track);
    EXPECT_EQ(field("standardComment")->text(), "Unsaved");
    auto other = track;
    other.id = {"other"};
    editor.show_track(&other);
    EXPECT_EQ(field("standardComment")->text(), "Unsaved");
    EXPECT_EQ(editor.edit_session().media(), std::vector<wiola::library::MediaId>{track.id});
    EXPECT_EQ(editor.edit_session().expectations().at(track.id).metadata_revision, 1);
    button("cancelStandardMetadata")->click();
    EXPECT_EQ(editor.edit_session().media(), std::vector<wiola::library::MediaId>{other.id});
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

    std::optional<wiola::library::StandardMetadataPatch> applied;
    editor.on_apply([&](const wiola::app::MetadataEditSession& session) {
        applied = session.draft();
    });
    field("standardGenre")->setText("Jazz");
    button("applyStandardMetadata")->click();

    ASSERT_TRUE(applied.has_value());
    EXPECT_EQ(*applied->genre.value(), "Jazz");
    EXPECT_EQ(applied->artist.operation(), wiola::library::FieldEditOperation::unchanged);
    EXPECT_EQ(applied->year.operation(), wiola::library::FieldEditOperation::unchanged);
    EXPECT_EQ(applied->title.operation(), wiola::library::FieldEditOperation::unchanged);
}

TEST_F(StandardMetadataEditor, ApplyAfterExternalChangeChecksOriginalTargetAndExpectations)
{
    wiola::library::InMemoryRepository repository;
    auto other = track;
    other.id = {"other"};
    ASSERT_TRUE(repository.put_track(track));
    ASSERT_TRUE(repository.put_track(other));
    wiola::app::MetadataEditService commands{repository, [](const auto&) { return true; },
        [](const auto&) { }};
    field("standardComment")->setText("Draft");
    track.embedded.artist = "External";
    ++track.metadata_revision;
    ASSERT_TRUE(repository.put_track(track));
    editor.show_track(repository.track(track.id));
    editor.show_track(repository.track(other.id));
    std::optional<wiola::app::MetadataEditOutcome> outcome;
    editor.on_apply([&](const wiola::app::MetadataEditSession& session) {
        outcome = commands.apply(session.media(), session.draft(), session.expectations());
    });
    button("applyStandardMetadata")->click();
    ASSERT_TRUE(outcome);
    EXPECT_EQ(outcome->state, wiola::app::MetadataEditState::conflicted);
    EXPECT_EQ(editor.edit_session().media(), std::vector<wiola::library::MediaId>{track.id});
    EXPECT_FALSE(repository.track(other.id)->overrides.comment);
    EXPECT_EQ(field("standardComment")->text(), "Draft");
}

TEST_F(StandardMetadataEditor, SessionDeduplicatesOccurrencesAndRetainsOwnedOriginalValues)
{
    auto other = track;
    other.id = {"other"};
    other.embedded.artist = "Different";
    const auto session =
        wiola::app::MetadataEditSession::capture({&track, &other, &track, nullptr});
    ASSERT_EQ(session.targets().size(), 2);
    EXPECT_TRUE(session.mixed(wiola::app::StandardMetadataField::artist));
    track.embedded.artist = "Later";
    track.content_revision = 99;
    track.fingerprint = "later";
    const auto found = std::ranges::find(session.targets(), track.id,
        &wiola::app::MetadataEditSession::Target::media);
    ASSERT_NE(found, session.targets().end());
    EXPECT_EQ(found->original.artist, "Embedded artist");
    EXPECT_EQ(found->expected.content_revision, 0);
    EXPECT_TRUE(found->expected.fingerprint.empty());
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
