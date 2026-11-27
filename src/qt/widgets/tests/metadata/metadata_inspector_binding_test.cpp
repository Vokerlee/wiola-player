/**
 * @file
 * @brief Metadata editor command wiring, conflicts and user-facing outcome contracts.
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

#include <fakes/output.hpp>
#include <qt/application/session/session_preferences.hpp>
#include <qt/widgets/metadata/metadata_edit_presentation.hpp>
#include <qt/widgets/metadata/metadata_inspector_binding.hpp>
#include <wiola/library/tags.hpp>
#include <wiola/media/codecs/ffmpeg/composition.hpp>
#include <wiola/qt/paths.hpp>

#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {

using namespace wiola;

TEST(MetadataPresentation, FileStagesDistinguishRetainedOverridesAndModifiedSourceFailures)
{
    app::MetadataWriteResult result;
    for (const auto& [stage, text] : std::vector<std::pair<app::MetadataWriteStage, QString>>{
             {app::MetadataWriteStage::queued,     "queued"                 },
             {app::MetadataWriteStage::remuxing,   "original remains usable"},
             {app::MetadataWriteStage::validating, "unchanged audio"        },
             {app::MetadataWriteStage::ready,      "validated"              },
             {app::MetadataWriteStage::reloading,  "reloading playback"     },
             {app::MetadataWriteStage::complete,   "Saved embedded"         },
             {app::MetadataWriteStage::conflicted, "not overwritten"        },
             {app::MetadataWriteStage::cancelled,  "overrides remain"       }
    }) {
        result.stage = stage;
        EXPECT_TRUE(gui::metadata_file_message(result).contains(text, Qt::CaseInsensitive));
    }
    result.stage = app::MetadataWriteStage::failed;
    result.outcome.error = media::MetadataWriteError::unsupported;
    EXPECT_TRUE(gui::metadata_file_message(result).contains("does not support"));
    result.outcome.error = media::MetadataWriteError::io;
    result.outcome.detail = "Disk is full";
    EXPECT_TRUE(gui::metadata_file_message(result).contains("Disk is full"));
    EXPECT_TRUE(gui::metadata_file_message(result).contains("overrides remain"));
    result.outcome.source_modified = true;
    EXPECT_EQ(gui::metadata_file_message(result), "Disk is full");
}

TEST(MetadataPresentation, EditOutcomesExplainDurabilityConflictsMissingTracksAndInvalidDrafts)
{
    app::MetadataEditOutcome result;
    result.state = app::MetadataEditState::accepted;
    result.revision = 42;
    EXPECT_TRUE(gui::metadata_edit_message(result).contains("42"));
    result.state = app::MetadataEditState::conflicted;
    EXPECT_TRUE(gui::metadata_edit_message(result).contains("review"));
    result.state = app::MetadataEditState::failed;
    result.retained_changes = true;
    EXPECT_TRUE(gui::metadata_edit_message(result).contains("Retry saving"));
    result.retained_changes = false;
    for (const auto& [error, text] : std::vector<std::pair<app::StandardMetadataError, QString>>{
             {app::StandardMetadataError::empty_edit,         "no metadata changes"},
             {app::StandardMetadataError::invalid_edit,       "invalid"            },
             {app::StandardMetadataError::persistence_failed, "Could not save"     }
    }) {
        result.error = error;
        EXPECT_TRUE(gui::metadata_edit_message(result).contains(text, Qt::CaseInsensitive));
    }
    result.state = app::MetadataEditState::succeeded;
    EXPECT_TRUE(gui::metadata_edit_message(result).contains("No metadata changes"));
    result.missing = 2;
    EXPECT_TRUE(gui::metadata_edit_message(result).contains("No selected tracks"));
    result.changed = 3;
    EXPECT_TRUE(gui::metadata_edit_message(result).contains("3 track(s); 2 unavailable"));
    result.missing = 0;
    EXPECT_TRUE(gui::metadata_edit_message(result).contains("3 track(s)"));
}

class MetadataBinding : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(directory.isValid());
        library::TrackRecord record;
        record.id = media;
        record.embedded.title = "Original";
        library::RepositoryTransaction transaction{library.repository(), library.persistence()};
        ASSERT_TRUE(library.repository().put_track(record));
        ASSERT_TRUE(transaction.commit());
        show();
    }

    void show()
    {
        gui::InspectorContext context;
        context.origin = gui::InspectorContext::Origin::selection;
        context.editable = {*library.repository().track(media)};
        context.tracks = {
            gui::present_record(library::PlaylistEntryId{"entry"}, context.editable[0])};
        inspector.show_context(context);
    }

    void text(const char* name, const QString& value)
    {
        auto* field = inspector.findChild<QLineEdit*>(name);
        ASSERT_NE(field, nullptr);
        field->setText(value);
    }

    void click(const char* name)
    {
        auto* button = inspector.findChild<QPushButton*>(name);
        ASSERT_NE(button, nullptr);
        ASSERT_TRUE(button->isEnabled());
        button->click();
    }

    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    app::SessionPreferences preferences{settings};
    app::PlaybackService playback{wiola::testing::passive_output, codec::ffmpeg::make_provider()};
    app::LibrarySession library{
        platform::qt::QtPathAdapter::from_qt(directory.filePath("library.json")),
        codec::ffmpeg::make_provider()};
    gui::DocumentHost host{library, playback, preferences, {}};
    gui::TrackInspector inspector;
    QString status;
    gui::MetadataInspectorBinding binding{inspector, host, library, playback,
        [this](const QString& message) { status = message; }};
    library::MediaId media{"media"};
};

TEST_F(MetadataBinding, AddsEditsRemovesAndRejectsDuplicateOrEmptyCustomTags)
{
    text("customTagKey", "Mood");
    text("customTagValue", "Calm");
    click("addCustomTag");
    ASSERT_EQ(library.repository().track(media)->tags.size(), 1);
    EXPECT_EQ(status, "Custom tag added");
    show();
    text("customTagKey", "Mood");
    text("customTagValue", "Calm");
    click("addCustomTag");
    EXPECT_TRUE(status.contains("already exists"));
    text("customTagValue", "");
    click("addCustomTag");
    EXPECT_TRUE(status.contains("valid, non-empty"));

    inspector.findChild<QListWidget*>("customTagValues")->setCurrentRow(0);
    click("editCustomTag");
    EXPECT_TRUE(status.contains("No custom tag changes"));
    text("customTagValue", "Energetic");
    click("editCustomTag");
    EXPECT_EQ(library.repository().track(media)->tags[0].value, "Energetic");
    show();
    inspector.findChild<QListWidget*>("customTagValues")->setCurrentRow(0);
    click("removeCustomTag");
    EXPECT_TRUE(library.repository().track(media)->tags.empty());
    EXPECT_EQ(status, "Custom tag removed");
}

TEST_F(MetadataBinding, StaleTrackAndTagSelectionsProduceActionableErrors)
{
    library::TrackRecord stale;
    stale.id = library::MediaId{"removed"};
    inspector.custom_tags()->show_track(&stale);
    text("customTagKey", "Mood");
    text("customTagValue", "Calm");
    click("addCustomTag");
    EXPECT_TRUE(status.contains("no longer in the library"));

    stale = *library.repository().track(media);
    stale.tags = {
        {library::TagId{"removed-tag"}, "Mood", "Calm"}
    };
    inspector.custom_tags()->show_track(&stale);
    inspector.findChild<QListWidget*>("customTagValues")->setCurrentRow(0);
    click("removeCustomTag");
    EXPECT_TRUE(status.contains("no longer available"));
    click("editCustomTag");
    EXPECT_TRUE(status.contains("no longer available"));
}

TEST_F(MetadataBinding, ConflictedDraftCanKeepReloadMergeOrOverwriteWithoutLosingOtherFields)
{
    for (const auto* action : {"keepApplicationMetadata", "reloadEmbeddedMetadata",
             "mergeMetadataFields", "overwriteMetadataConflict"}) {
        SCOPED_TRACE(action);
        show();
        const auto draft = QString{"Draft %1"}.arg(action);
        text("standardTitle", draft);
        auto changed = *library.repository().track(media);
        changed.overrides.title = "Concurrent";
        changed.overrides.artist = "Other editor";
        ++changed.metadata_revision;
        library::RepositoryTransaction transaction{library.repository(), library.persistence()};
        ASSERT_TRUE(library.repository().put_track(changed));
        ASSERT_TRUE(transaction.commit());
        click("applyStandardMetadata");
        EXPECT_FALSE(inspector.conflicts()->isHidden());
        EXPECT_TRUE(inspector.standard_metadata()->retains_draft());
        click("reviewMetadataConflict");
        EXPECT_TRUE(inspector.standard_metadata()->retains_draft());
        click(action);
        EXPECT_FALSE(inspector.standard_metadata()->retains_draft());
        const auto* current = library.repository().track(media);
        if (QString{action} == "keepApplicationMetadata")
            EXPECT_EQ(current->display_title(), "Concurrent");
        else if (QString{action} == "reloadEmbeddedMetadata")
            EXPECT_EQ(current->display_title(), "Original");
        else {
            EXPECT_EQ(current->display_title(), draft.toStdString());
            EXPECT_EQ(current->display_artist(), "Other editor");
        }
    }
}

TEST_F(MetadataBinding, TagLimitAndPersistenceRejectionKeepTheCatalogUnchanged)
{
    auto full = *library.repository().track(media);
    for (std::size_t i = 0; i < library::max_tags_per_track; ++i)
        full.tags.push_back({library::TagId{"tag-" + std::to_string(i)}, "Key", std::to_string(i)});
    {
        library::RepositoryTransaction transaction{library.repository(), library.persistence()};
        ASSERT_TRUE(library.repository().put_track(full));
        ASSERT_TRUE(transaction.commit());
    }
    show();
    text("customTagKey", "New");
    text("customTagValue", "Value");
    click("addCustomTag");
    EXPECT_TRUE(status.contains("maximum number"));
    EXPECT_EQ(library.repository().track(media)->tags.size(), library::max_tags_per_track);

    full.tags.clear();
    ASSERT_TRUE(library.repository().put_track(full));
    ASSERT_TRUE(library.shutdown().succeeded());
    show();
    text("customTagKey", "New");
    text("customTagValue", "Value");
    click("addCustomTag");
    EXPECT_TRUE(status.contains("Could not save"));
    EXPECT_TRUE(library.repository().track(media)->tags.empty());
}

} // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "minimal");
    QApplication app{argc, argv};
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
