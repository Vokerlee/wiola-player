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

#include <fixtures/track_view.hpp>

#include <gui/documents/queue_rows.hpp>

#include <gui/tracks/track_table_model.hpp>

#include <engine/queue/playlist.hpp>
#include <library/in_memory_repository.hpp>
#include <library/location.hpp>

#include <QApplication>
#include <QBuffer>
#include <QFont>
#include <QImage>
#include <QPixmap>

#include <QAbstractItemModelTester>
#include <QPersistentModelIndex>
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

TEST_F(TrackTableModelTest, KeepsPersistentIndexesThroughInsertRemoveSortAndFilter)
{
    wiola::gui::TrackTableModel model{styled};
    QAbstractItemModelTester tester{&model, QAbstractItemModelTester::FailureReportingMode::Fatal};
    wiola::gui::TrackRow first, second, third;
    first.id = wiola::library::PlaylistEntryId{"one"};
    first.metadata.title = "Beta";
    second.id = wiola::library::PlaylistEntryId{"two"};
    second.metadata.title = "Alpha";
    third.id = wiola::library::PlaylistEntryId{"three"};
    third.metadata.title = "Gamma";
    model.show_rows({first, second});
    QPersistentModelIndex retained = model.index(1, 1);
    int resets = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
    model.show_rows({third, second, first});
    EXPECT_TRUE(retained.isValid());
    EXPECT_EQ(model.row_id(retained), second.id);
    wiola::testing::set_track_sort(model, {{.field = wiola::gui::TrackSortField::title}});
    EXPECT_EQ(retained.row(), 0);
    model.show_rows({second, third});
    EXPECT_EQ(model.row_id(retained), second.id);
    second.metadata.title = "Edited";
    model.show_rows({second, third});
    EXPECT_EQ(retained.data().toString(), "Edited");
    wiola::library::SearchQuery query;
    query.title = "gamma";
    wiola::testing::set_track_query(model, query);
    EXPECT_FALSE(retained.isValid());
    EXPECT_EQ(resets, 0);
    model.show_tag_keys({"Genre", "Mood"});
    model.show_tag_keys({"Mood"});
}

TEST_F(TrackTableModelTest, RejectsInvalidAndDuplicateOccurrenceIdentities)
{
    wiola::gui::TrackTableModel model{styled};
    QAbstractItemModelTester tester{&model, QAbstractItemModelTester::FailureReportingMode::Fatal};
    wiola::gui::TrackRow invalid, first, copy;
    first.id = wiola::library::PlaylistEntryId{"one"};
    first.metadata.title = "Original";
    copy = first;
    copy.metadata.title = "Duplicate";
    model.show_rows({invalid, first, copy});
    EXPECT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.index(0, 1).data().toString(), "Original");
}

} // namespace

TEST_F(TrackTableModelTest, ChangesOnlyTheRowWhoseMetadataChanged)
{
    wiola::gui::TrackTableModel model{styled};
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("first.wav"), wiola::engine::Track::of("second.wav")});
    model.show_rows(wiola::gui::queue_rows(playlist.tracks()));

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
            .duration = {},
            .genre = {}});
    model.show_rows(wiola::gui::queue_rows(playlist.tracks()));

    EXPECT_EQ(changes, 1);
    EXPECT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.data(model.index(1, 1), Qt::DisplayRole).toString(), "new title");
}

TEST_F(TrackTableModelTest, AddressesRowsByStableTrackIdentity)
{
    wiola::gui::TrackTableModel model{styled};
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("first.wav"), wiola::engine::Track::of("second.wav")});
    model.show_rows(wiola::gui::queue_rows(playlist.tracks()));

    EXPECT_EQ(wiola::gui::queue_entry_id(model.row_id(model.index(0, 0))), id_at(playlist, 0));
    EXPECT_EQ(wiola::gui::queue_entry_id(model.row_id(model.index(1, 1))), id_at(playlist, 1));
    EXPECT_FALSE(wiola::gui::queue_entry_id(model.row_id(model.index(2, 0))).has_value());
    EXPECT_FALSE(wiola::gui::queue_entry_id(model.row_id({})).has_value());

    const auto removed = id_at(playlist, 0);
    playlist.remove(0);
    model.show_rows(wiola::gui::queue_rows(playlist.tracks()));

    EXPECT_NE(wiola::gui::queue_entry_id(model.row_id(model.index(0, 0))), removed);
    EXPECT_EQ(wiola::gui::queue_entry_id(model.row_id(model.index(0, 0))), id_at(playlist, 0));
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

    model.show_rows(wiola::gui::queue_rows(original), id_at(first, 1));
    model.show_rows(wiola::gui::queue_rows(reordered), id_at(first, 1));

    EXPECT_TRUE(model.data(model.index(0, 1), Qt::FontRole).value<QFont>().bold());
    EXPECT_FALSE(model.data(model.index(1, 1), Qt::FontRole).value<QFont>().bold());
}

TEST_F(TrackTableModelTest, UpdatesSelectionWithoutDependingOnPlaylistRevisions)
{
    wiola::gui::TrackTableModel model{styled};
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("first.wav"), wiola::engine::Track::of("second.wav")});
    model.show_rows(wiola::gui::queue_rows(playlist.tracks()), id_at(playlist, 0));

    int changes{0};
    QObject::connect(&model, &QAbstractItemModel::dataChanged,
        [&changes](const QModelIndex&, const QModelIndex&, const QList<int>&) { ++changes; });

    model.show_rows(wiola::gui::queue_rows(playlist.tracks()), id_at(playlist, 1));

    EXPECT_EQ(changes, 2);
    EXPECT_FALSE(model.data(model.index(0, 1), Qt::FontRole).value<QFont>().bold());
    EXPECT_TRUE(model.data(model.index(1, 1), Qt::FontRole).value<QFont>().bold());

    model.show_rows(wiola::gui::queue_rows(playlist.tracks()));
    EXPECT_FALSE(model.data(model.index(1, 1), Qt::FontRole).value<QFont>().bold());
}

TEST_F(TrackTableModelTest, SharesArtworkAcrossDuplicatesAndInvalidatesANewRevision)
{
    wiola::gui::TrackTableModel model{styled};
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("same.wav"), wiola::engine::Track::of("same.wav")});
    const std::vector entries{id_at(playlist, 0), id_at(playlist, 1)};

    playlist.apply_metadata(entries, {}, sample_artwork(Qt::GlobalColor::blue));
    model.show_rows(wiola::gui::queue_rows(playlist.tracks()));

    const QPixmap first_blue = cover_at(model, 0);
    const QPixmap second_blue = cover_at(model, 1);
    EXPECT_EQ(first_blue.cacheKey(), second_blue.cacheKey());
    EXPECT_EQ(first_blue.toImage().pixelColor(0, 0), QColor{Qt::GlobalColor::blue});

    playlist.apply_metadata(entries, {}, sample_artwork(Qt::GlobalColor::red));
    model.show_rows(wiola::gui::queue_rows(playlist.tracks()));

    const QPixmap first_red = cover_at(model, 0);
    const QPixmap second_red = cover_at(model, 1);
    EXPECT_NE(first_red.cacheKey(), first_blue.cacheKey());
    EXPECT_EQ(first_red.cacheKey(), second_red.cacheKey());
    EXPECT_EQ(first_red.toImage().pixelColor(0, 0), QColor{Qt::GlobalColor::red});
}

TEST_F(TrackTableModelTest, PresentsDynamicTagColumnsAndLiveValues)
{
    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord record;
    record.id = wiola::library::media_id_for("first.wav");
    record.tags = {
        {.id = wiola::library::TagId{"tag-1"}, .key = "Genre", .value = "Jazz"  },
        {.id = wiola::library::TagId{"tag-2"}, .key = "Genre", .value = "Fusion"},
        {.id = wiola::library::TagId{"tag-3"}, .key = "Mood",  .value = "Calm"  }
    };
    ASSERT_TRUE(repository.put_track(record));

    wiola::gui::TrackTableModel model{styled};
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("first.wav"), wiola::engine::Track::of("second.wav")});
    model.show_rows(wiola::gui::queue_rows(playlist.tracks()));
    model.show_library(repository);
    model.show_tag_keys({"Genre", "genre", "Mood"});

    EXPECT_EQ(model.columnCount(), 7);
    EXPECT_EQ(model.headerData(5, Qt::Horizontal, Qt::DisplayRole).toString(), "Genre");
    EXPECT_EQ(model.headerData(6, Qt::Horizontal, Qt::DisplayRole).toString(), "Mood");
    EXPECT_EQ(model.data(model.index(0, 5), Qt::DisplayRole).toString(), "Jazz, Fusion");
    EXPECT_EQ(model.data(model.index(0, 6), Qt::DisplayRole).toString(), "Calm");
    EXPECT_TRUE(model.data(model.index(1, 5), Qt::DisplayRole).toString().isEmpty());

    record.tags[1].value = "Live";
    ++record.metadata_revision;
    ASSERT_TRUE(repository.put_track(record));
    model.show_library(repository);

    EXPECT_EQ(model.data(model.index(0, 5), Qt::DisplayRole).toString(), "Jazz, Live");

    wiola::library::SearchQuery query;
    query.filters = {
        {.key = "genre", .value = "live", .mode = wiola::library::TagFilterMode::include}
    };
    wiola::testing::set_track_query(model, query);
    EXPECT_EQ(model.rowCount(), 1);

    query.filters = {
        {.key = "mood", .value = "calm", .mode = wiola::library::TagFilterMode::exclude}
    };
    wiola::testing::set_track_query(model, query);
    EXPECT_EQ(model.rowCount(), 1);
    EXPECT_EQ(wiola::gui::queue_entry_id(model.row_id(model.index(0, 0))), id_at(playlist, 1));

    wiola::testing::set_track_query(model, {});
    EXPECT_EQ(model.rowCount(), 2);
}

TEST_F(TrackTableModelTest, FiltersSimultaneousTitleArtistAndAlbumTerms)
{
    wiola::library::InMemoryRepository repository;
    wiola::gui::TrackTableModel model{styled};
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("first.wav"), wiola::engine::Track::of("second.wav")});
    playlist.describe(id_at(playlist, 0),
        {.title = "My Song",
            .artist = "The Artist",
            .album = "Great Album",
            .duration = {},
            .genre = {}});
    playlist.describe(id_at(playlist, 1),
        {.title = "My Song",
            .artist = "Other Artist",
            .album = "Great Album",
            .duration = {},
            .genre = {}});
    model.show_library(repository);
    model.show_rows(wiola::gui::queue_rows(playlist.tracks()));

    wiola::testing::set_track_query(model,
        {.title = "song", .artist = "the artist", .album = "great", .tags = {}, .filters = {}});

    EXPECT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.total_count(), 2);
    EXPECT_EQ(wiola::gui::queue_entry_id(model.row_id(model.index(0, 0))), id_at(playlist, 0));

    wiola::testing::set_track_query(model,
        {.title = "song", .artist = "missing", .album = "great", .tags = {}, .filters = {}});
    EXPECT_EQ(model.rowCount(), 0);
    EXPECT_EQ(model.total_count(), 2);

    wiola::testing::set_track_query(model, {});
    EXPECT_EQ(model.rowCount(), 2);
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
