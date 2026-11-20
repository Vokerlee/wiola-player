/**
 * @file
 * @brief Identity-based track table model regression tests.
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

#include <gui/track_table_model.hpp>

#include <engine/queue/playlist.hpp>

#include <QApplication>
#include <QBuffer>
#include <QFont>
#include <QImage>
#include <QPixmap>

#include <gtest/gtest.h>

#include <cstring>

namespace {

class TrackTableModelTest : public testing::Test {
protected:
    QWidget styled;
};

wiola::engine::QueueEntryId id_at(const wiola::engine::Playlist& playlist, std::size_t row)
{
    return playlist.tracks().at(row).id();
}

std::shared_ptr<wiola::media::Artwork> sample_artwork(Qt::GlobalColor color)
{
    QImage image{4, 4, QImage::Format_ARGB32};
    image.fill(color);

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

QPixmap cover_at(wiola::gui::TrackTableModel& model, int row)
{
    return model.data(model.index(row, 0), Qt::DecorationRole).value<QPixmap>();
}

} // namespace

TEST_F(TrackTableModelTest, ChangesOnlyTheRowWhoseMetadataChanged)
{
    wiola::gui::TrackTableModel model{styled};
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("first.wav"), wiola::engine::Track::of("second.wav")});
    model.show_tracks(playlist.tracks());

    int changes{0};
    QObject::connect(&model, &QAbstractItemModel::dataChanged,
        [&changes](const QModelIndex& first, const QModelIndex& last, const QList<int>&) {
            EXPECT_EQ(first.row(), last.row());
            ++changes;
        });

    playlist.describe(id_at(playlist, 1),
        wiola::engine::TrackMetadata{.title = "new title",
            .artist = {},
            .album = {},
            .duration = {}});
    model.show_tracks(playlist.tracks());

    EXPECT_EQ(changes, 1);
    EXPECT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.data(model.index(1, 1), Qt::DisplayRole).toString(), "new title");
}

TEST_F(TrackTableModelTest, AddressesRowsByStableTrackIdentity)
{
    wiola::gui::TrackTableModel model{styled};
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("first.wav"), wiola::engine::Track::of("second.wav")});
    model.show_tracks(playlist.tracks());

    EXPECT_EQ(model.track_id(model.index(0, 0)), id_at(playlist, 0));
    EXPECT_EQ(model.track_id(model.index(1, 1)), id_at(playlist, 1));
    EXPECT_FALSE(model.track_id(model.index(2, 0)).has_value());
    EXPECT_FALSE(model.track_id({}).has_value());

    const auto removed = id_at(playlist, 0);
    playlist.remove(0);
    model.show_tracks(playlist.tracks());

    EXPECT_NE(model.track_id(model.index(0, 0)), removed);
    EXPECT_EQ(model.track_id(model.index(0, 0)), id_at(playlist, 0));
}

TEST_F(TrackTableModelTest, FollowsTheCurrentIdentityAcrossReordering)
{
    wiola::gui::TrackTableModel model{styled};
    wiola::engine::Playlist first;
    first.set({wiola::engine::Track::of("first.wav"), wiola::engine::Track::of("second.wav")});

    std::vector<wiola::engine::Track> original{first.tracks()};
    std::vector<wiola::engine::Track> reordered{original};
    reordered[0] = original[1];
    reordered[1] = original[0];

    model.show_tracks(original, id_at(first, 1));
    model.show_tracks(reordered, id_at(first, 1));

    EXPECT_TRUE(model.data(model.index(0, 1), Qt::FontRole).value<QFont>().bold());
    EXPECT_FALSE(model.data(model.index(1, 1), Qt::FontRole).value<QFont>().bold());
}

TEST_F(TrackTableModelTest, UpdatesSelectionWithoutDependingOnPlaylistRevisions)
{
    wiola::gui::TrackTableModel model{styled};
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("first.wav"), wiola::engine::Track::of("second.wav")});
    model.show_tracks(playlist.tracks(), id_at(playlist, 0));

    int changes{0};
    QObject::connect(&model, &QAbstractItemModel::dataChanged,
        [&changes](const QModelIndex&, const QModelIndex&, const QList<int>&) { ++changes; });

    model.show_tracks(playlist.tracks(), id_at(playlist, 1));

    EXPECT_EQ(changes, 2);
    EXPECT_FALSE(model.data(model.index(0, 1), Qt::FontRole).value<QFont>().bold());
    EXPECT_TRUE(model.data(model.index(1, 1), Qt::FontRole).value<QFont>().bold());

    model.show_tracks(playlist.tracks());
    EXPECT_FALSE(model.data(model.index(1, 1), Qt::FontRole).value<QFont>().bold());
}

TEST_F(TrackTableModelTest, SharesArtworkAcrossDuplicatesAndInvalidatesANewRevision)
{
    wiola::gui::TrackTableModel model{styled};
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("same.wav"), wiola::engine::Track::of("same.wav")});
    const std::vector entries{id_at(playlist, 0), id_at(playlist, 1)};

    playlist.apply_metadata(entries, {}, sample_artwork(Qt::GlobalColor::blue));
    model.show_tracks(playlist.tracks());

    const QPixmap first_blue = cover_at(model, 0);
    const QPixmap second_blue = cover_at(model, 1);
    EXPECT_EQ(first_blue.cacheKey(), second_blue.cacheKey());
    EXPECT_EQ(first_blue.toImage().pixelColor(0, 0), QColor{Qt::GlobalColor::blue});

    playlist.apply_metadata(entries, {}, sample_artwork(Qt::GlobalColor::red));
    model.show_tracks(playlist.tracks());

    const QPixmap first_red = cover_at(model, 0);
    const QPixmap second_red = cover_at(model, 1);
    EXPECT_NE(first_red.cacheKey(), first_blue.cacheKey());
    EXPECT_EQ(first_red.cacheKey(), second_red.cacheKey());
    EXPECT_EQ(first_red.toImage().pixelColor(0, 0), QColor{Qt::GlobalColor::red});
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
