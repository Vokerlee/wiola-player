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

#include <gui/metadata/track_inspector.hpp>
#include <platform/qt/qt_path_adapter.hpp>

#include <engine/queue/playlist.hpp>

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
    inspector.show_track(&playlist.tracks()[0]);

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
    inspector.show_track(&playlist.tracks()[0]);

    EXPECT_EQ(field(inspector, "inspectorAvailability")->text(), "Missing");
    EXPECT_TRUE(field(inspector, "inspectorAvailability")->property("missing").toBool());
    EXPECT_EQ(field(inspector, "inspectorTitle")->text(), "missing");
    EXPECT_EQ(field(inspector, "inspectorArtist")->text(), "Unknown artist");
    EXPECT_EQ(field(inspector, "inspectorAlbum")->text(), "Unknown album");
    EXPECT_EQ(field(inspector, "inspectorDuration")->text(), "Unknown");

    inspector.show_track(nullptr);

    EXPECT_EQ(field(inspector, "inspectorTitle")->text(), "No track selected");
    EXPECT_TRUE(field(inspector, "inspectorArtist")->text().isEmpty());
    EXPECT_TRUE(field(inspector, "inspectorAvailability")->text().isEmpty());
    EXPECT_FALSE(field(inspector, "inspectorAvailability")->property("missing").toBool());
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
