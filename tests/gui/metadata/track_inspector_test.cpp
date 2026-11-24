/**
 * @file
 * @brief Read-only track inspector regression tests.
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

#include <codec/provider.hpp>
#include <desktop/library/library_session.hpp>
#include <gui/metadata/track_inspector.hpp>
#include <gui/model/documents/queue_rows.hpp>
#include <platform/qt/qt_path_adapter.hpp>

#include <queue/playlist.hpp>

#include <QApplication>
#include <QBuffer>
#include <QImage>
#include <QLabel>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>

namespace {

QLabel* field(QWidget& inspector, const QString& name)
{
    return inspector.findChild<QLabel*>(name);
}

std::filesystem::path temporary_path(const QTemporaryDir& directory, const char* name)
{
    return wiola::platform::qt::QtPathAdapter::from_qt(directory.filePath(name));
}

std::shared_ptr<wiola::media::Artwork> sample_artwork()
{
    QImage image{4, 4, QImage::Format_ARGB32};
    image.fill(Qt::GlobalColor::blue);

    QByteArray encoded;
    QBuffer buffer{&encoded};
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");

    auto artwork = std::make_shared<wiola::media::Artwork>();
    artwork->mime_type = "image/png";
    artwork->data.resize(static_cast<std::size_t>(encoded.size()));
    std::memcpy(artwork->data.data(), encoded.constData(), artwork->data.size());

    return artwork;
}

TEST(TrackInspector, ShowsMetadataLocationDurationArtworkAndAvailability)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    const std::filesystem::path path = temporary_path(directory, "track.wav");
    std::ofstream{path} << "audio";

    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of(path)});
    const auto id = playlist.tracks()[0].id();
    playlist.describe(id,
        wiola::engine::TrackMetadata{.title = "Title",
            .artist = "Artist",
            .album = "Album",
            .duration = wiola::units::Time{65}});
    playlist.illustrate(id, sample_artwork());

    wiola::gui::TrackInspector inspector;
    wiola::gui::InspectorContext context;
    context.origin = wiola::gui::InspectorContext::Origin::selection;
    auto row = wiola::gui::queue_row(playlist.tracks()[0]);
    wiola::library::TrackRecord record;
    record.id = row.media;
    record.locations = {row.location};
    record.embedded = row.metadata;
    context.editable.push_back(record);
    context.tracks.push_back(row);
    std::filesystem::remove(path); // Availability is a cached fact, never a refresh-time probe.
    inspector.show_context(context);

    ASSERT_NE(field(inspector, "inspectorArtwork"), nullptr);
    EXPECT_FALSE(field(inspector, "inspectorArtwork")->pixmap().isNull());
    EXPECT_EQ(field(inspector, "inspectorTitle")->text(), "Title");
    EXPECT_EQ(field(inspector, "inspectorArtist")->text(), "Artist");
    EXPECT_EQ(field(inspector, "inspectorAlbum")->text(), "Album");
    EXPECT_EQ(field(inspector, "inspectorLocation")->text(), QString::fromStdString(path.string()));
    EXPECT_EQ(field(inspector, "inspectorDuration")->text(), "1:05");
    EXPECT_EQ(field(inspector, "inspectorAvailability")->text(), "Available");
    EXPECT_FALSE(field(inspector, "inspectorAvailability")->property("missing").toBool());
}

TEST(TrackInspector, MarksMissingFilesAndClearsWhenThereIsNoSelection)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of(temporary_path(directory, "missing.flac"))});

    wiola::gui::TrackInspector inspector;
    wiola::gui::InspectorContext context;
    context.origin = wiola::gui::InspectorContext::Origin::selection;
    auto row = wiola::gui::queue_row(playlist.tracks()[0]);
    row.missing = true;
    wiola::library::TrackRecord record;
    record.id = row.media;
    record.locations = {row.location};
    record.embedded = row.metadata;
    record.missing = true;
    context.editable.push_back(record);
    context.tracks.push_back(row);
    inspector.show_context(context);

    EXPECT_EQ(field(inspector, "inspectorAvailability")->text(), "Missing");
    EXPECT_TRUE(field(inspector, "inspectorAvailability")->property("missing").toBool());
    EXPECT_EQ(field(inspector, "inspectorTitle")->text(), "missing");
    EXPECT_EQ(field(inspector, "inspectorArtist")->text(), "Unknown artist");
    EXPECT_EQ(field(inspector, "inspectorAlbum")->text(), "Unknown album");
    EXPECT_EQ(field(inspector, "inspectorDuration")->text(), "Unknown");

    inspector.show_context({});

    EXPECT_EQ(field(inspector, "inspectorTitle")->text(), "No track selected");
    EXPECT_TRUE(field(inspector, "inspectorArtist")->text().isEmpty());
    EXPECT_TRUE(field(inspector, "inspectorAvailability")->text().isEmpty());
    EXPECT_FALSE(field(inspector, "inspectorAvailability")->property("missing").toBool());
}

TEST(TrackInspector, ExplicitSelectionOwnsEverySectionAndCustomTagTargetWhileAnotherTrackPlays)
{
    QTemporaryDir directory;
    wiola::app::LibrarySession library{temporary_path(directory, "library.json"),
        std::make_shared<wiola::codec::LocalMediaProvider>()};
    wiola::library::TrackRecord selected, playing;
    selected.id = {"selected"};
    selected.locations = {"selected.wav"};
    selected.embedded.title = "Selected title";
    selected.embedded.artist = "Selected artist";
    selected.embedded.album = "Selected album";
    selected.tags = {
        {.id = {"tag"}, .key = "Mood", .value = "Calm"}
    };
    playing = selected;
    playing.id = {"playing"};
    playing.embedded.title = "Playing title";
    playing.locations = {"playing.wav"};
    {
        wiola::library::RepositoryTransaction seed{library.repository(), library.persistence()};
        library.repository().put_track(selected);
        library.repository().put_track(playing);
        ASSERT_TRUE(seed.commit());
    }
    auto row = wiola::gui::present_record(wiola::engine::QueueEntryId{2}, playing);
    row.artwork = sample_artwork();
    auto context = wiola::gui::inspector_context(library,
        {
            {selected.id, wiola::library::PlaylistEntryId{"entry"}, selected.locations.front()}
    },
        row);
    // Mutating the source record after capture cannot change the presentation snapshot.
    selected.embedded.title = "Later";
    {
        wiola::library::RepositoryTransaction change{library.repository(), library.persistence()};
        library.repository().put_track(selected);
        ASSERT_TRUE(change.commit());
    }
    // The assertion concerns the captured selection, after its seed writes are settled.
    ASSERT_TRUE(library.flush().succeeded());
    wiola::gui::TrackInspector inspector;
    inspector.show_context(context);
    EXPECT_EQ(field(inspector, "inspectorTitle")->text(), "Selected title");
    EXPECT_EQ(field(inspector, "inspectorArtist")->text(), "Selected artist");
    EXPECT_EQ(inspector.findChild<QLineEdit*>("standardTitle")->text(), "Selected title");
    EXPECT_EQ(inspector.findChild<QListWidget*>("customTagValues")->item(0)->text(), "Mood: Calm");
    EXPECT_FALSE(inspector.context().tracks.front().artwork);
    EXPECT_EQ(inspector.standard_metadata()->edit_session().media(),
        std::vector<wiola::library::MediaId>{selected.id});
    wiola::library::MediaId target;
    inspector.custom_tags()->on_add([&](auto media, auto, auto) { target = media; });
    inspector.findChild<QLineEdit*>("customTagKey")->setText("Genre");
    inspector.findChild<QLineEdit*>("customTagValue")->setText("Jazz");
    inspector.findChild<QPushButton*>("addCustomTag")->click();
    EXPECT_EQ(target, selected.id);
    auto fallback = wiola::gui::inspector_context(library, {}, row);
    inspector.show_context(fallback);
    EXPECT_TRUE(field(inspector, "inspectorContext")->text().contains("Now Playing"));
    EXPECT_EQ(field(inspector, "inspectorTitle")->text(), "Playing title");
    inspector.show_context(wiola::gui::inspector_context(library, {}));
    EXPECT_EQ(field(inspector, "inspectorTitle")->text(), "No track selected");
}

TEST(TrackInspector, RetainedDraftPinsAllSectionsUntilExplicitCancel)
{
    wiola::gui::InspectorContext first, second;
    first.origin = second.origin = wiola::gui::InspectorContext::Origin::selection;
    wiola::library::TrackRecord record;
    record.id = {"first"};
    record.locations = {"first.wav"};
    record.embedded.title = "First";
    first.editable = {record};
    first.tracks = {wiola::gui::present_record(wiola::library::PlaylistEntryId{"one"}, record)};
    record.id = {"second"};
    record.embedded.title = "Second";
    second.editable = {record};
    second.tracks = {wiola::gui::present_record(wiola::library::PlaylistEntryId{"two"}, record)};
    wiola::gui::TrackInspector inspector;
    inspector.show_context(first);
    inspector.findChild<QLineEdit*>("standardTitle")->setText("Draft");
    inspector.show_context(second);
    EXPECT_EQ(field(inspector, "inspectorTitle")->text(), "First");
    EXPECT_EQ(inspector.findChild<QLineEdit*>("standardTitle")->text(), "Draft");
    EXPECT_TRUE(field(inspector, "inspectorContext")->text().contains("original selection"));
    inspector.findChild<QPushButton*>("cancelStandardMetadata")->click();
    EXPECT_EQ(field(inspector, "inspectorTitle")->text(), "Second");
    EXPECT_EQ(inspector.findChild<QLineEdit*>("standardTitle")->text(), "Second");
    EXPECT_EQ(inspector.standard_metadata()->edit_session().media(),
        std::vector<wiola::library::MediaId>{record.id});
}

TEST(TrackInspector, MultiSelectionUsesMixedEditorAndDisablesSingleTrackCustomTags)
{
    wiola::gui::InspectorContext context;
    context.origin = wiola::gui::InspectorContext::Origin::selection;
    for (const auto& id : {"one", "two"}) {
        wiola::library::TrackRecord record;
        record.id = {id};
        record.locations = {std::string{id} + ".wav"};
        record.embedded.title = id;
        context.editable.push_back(record);
        context.tracks.push_back(wiola::gui::present_record(wiola::library::PlaylistEntryId{id},
            record));
    }
    wiola::gui::TrackInspector inspector;
    inspector.show_context(context);
    EXPECT_TRUE(field(inspector, "inspectorTitle")->text().contains("2 selected tracks"));
    EXPECT_EQ(inspector.findChild<QLineEdit*>("standardTitle")->placeholderText(), "Mixed values");
    EXPECT_FALSE(inspector.findChild<QLineEdit*>("customTagKey")->isEnabled());
    EXPECT_EQ(inspector.standard_metadata()->edit_session().targets().size(), 2);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
