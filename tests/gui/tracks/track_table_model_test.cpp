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

#include <gui/model/documents/queue_rows.hpp>

#include <gui/tracks/track_table_model.hpp>

#include <library/in_memory_repository.hpp>
#include <library/location.hpp>
#include <queue/playlist.hpp>

#include <QApplication>
#include <QBuffer>
#include <QEventLoop>
#include <QFont>
#include <QImage>
#include <QPixmap>
#include <QSignalSpy>
#include <QTimer>

#include <QAbstractItemModelTester>
#include <QPersistentModelIndex>
#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <tuple>
#include <utility>

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
    playlist.set({"first.wav", "second.wav"});
    model.show_rows(wiola::gui::queue_rows(playlist.tracks()));

    int changes{0};
    QObject::connect(&model, &QAbstractItemModel::dataChanged,
        [&changes](const QModelIndex& first, const QModelIndex& last, const QList<int>&) {
            EXPECT_EQ(first.row(), last.row());
            ++changes;
        });

    wiola::media::TrackMetadata metadata;
    metadata.title = "new title";
    playlist.describe(id_at(playlist, 1), std::move(metadata));
    model.show_rows(wiola::gui::queue_rows(playlist.tracks()));

    EXPECT_EQ(changes, 1);
    EXPECT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.data(model.index(1, 1), Qt::DisplayRole).toString(), "new title");
}

TEST_F(TrackTableModelTest, AddressesRowsByStableTrackIdentity)
{
    wiola::gui::TrackTableModel model{styled};
    wiola::engine::Playlist playlist;
    playlist.set({"first.wav", "second.wav"});
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
    first.set({"first.wav", "second.wav"});

    std::vector<wiola::engine::QueueEntry> original{first.tracks()};
    std::vector<wiola::engine::QueueEntry> reordered{original};
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
    playlist.set({"first.wav", "second.wav"});
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
    playlist.set({"same.wav", "same.wav"});
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
    playlist.set({"first.wav", "second.wav"});
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
    playlist.set({"first.wav", "second.wav"});
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

TEST_F(TrackTableModelTest, LargeQueriesRejectOldResultsReuseSnapshotsAndKeepPersistentIdentity)
{
    using namespace wiola;
    std::vector<gui::TrackRow> rows;
    for (int i = 0; i < 5000; ++i) {
        gui::TrackRow row;
        row.id = engine::QueueEntryId{static_cast<std::uint64_t>(i + 1)};
        row.media = {std::to_string(i)};
        row.metadata.title = "Track " + std::to_string(i);
        row.location_key = std::to_string(i);
        rows.push_back(std::move(row));
    }
    gui::TrackTableModel model{styled};
    model.show_rows(rows);
    QPersistentModelIndex retained = model.index(4999, 1);
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    model.on_async_projection([&](auto apply) {
        apply();
        loop.quit();
    });
    gui::TrackViewState first;
    first.query.title = "Track 4";
    ASSERT_TRUE(model.set_view_state(first));
    EXPECT_TRUE(model.projection_pending());
    auto last = first;
    last.query.title = "Track 4999";
    ASSERT_TRUE(model.set_view_state(last));
    deadline.start(10000);
    loop.exec();
    EXPECT_FALSE(model.projection_pending());
    ASSERT_EQ(model.rowCount(), 1);
    EXPECT_TRUE(retained.isValid());
    EXPECT_EQ(retained.row(), 0);
    EXPECT_EQ(retained.data().toString(), "Track 4999");
    EXPECT_EQ(model.projection_statistics().snapshots, 1);
    EXPECT_EQ(model.projection_statistics().source_rebuilds, 1);
    EXPECT_EQ(model.projection_statistics().commits, 2);
    last.query.title = "not present";
    ASSERT_TRUE(model.set_view_state(last));
    deadline.start(10000);
    loop.exec();
    EXPECT_EQ(model.rowCount(), 0);
    EXPECT_FALSE(retained.isValid());
    EXPECT_EQ(model.projection_statistics().snapshots, 1);
}

TEST_F(TrackTableModelTest, ARowPatchUpdatesOnlyItsOccurrenceWithoutRebuildingTheSourceOrQuery)
{
    using namespace wiola;
    std::vector<gui::TrackRow> rows;
    for (int i = 0; i < 1000; ++i) {
        gui::TrackRow row;
        row.id = engine::QueueEntryId{static_cast<std::uint64_t>(i + 1)};
        row.media = {std::to_string(i)};
        row.metadata.title = "Track " + std::to_string(i);
        rows.push_back(std::move(row));
    }
    gui::TrackTableModel model{styled};
    model.show_rows(rows);
    const auto before = model.projection_statistics();
    QSignalSpy changed{&model, &QAbstractItemModel::dataChanged};
    auto row = rows[517];
    row.metadata.title = "Updated";
    model.update_rows({row});
    EXPECT_EQ(changed.size(), 1);
    EXPECT_EQ(model.index(517, 1).data().toString(), "Updated");
    EXPECT_EQ(model.index(516, 1).data().toString(), "Track 516");
    EXPECT_EQ(model.projection_statistics().source_rebuilds, before.source_rebuilds);
    EXPECT_EQ(model.projection_statistics().queries, before.queries);
    EXPECT_EQ(model.projection_statistics().updated_rows, 1);
}

namespace {

std::string folded(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

TEST_F(TrackTableModelTest, BoundedGeneratedMutationsMatchAnIndependentIdentityOracle)
{
    using namespace wiola;
    gui::TrackTableModel model{styled};
    QAbstractItemModelTester tester{&model, QAbstractItemModelTester::FailureReportingMode::Fatal};
    int resets = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
    std::vector<gui::TrackRow> source;
    int next = 0;
    for (int step = 0; step < 160; ++step) {
        SCOPED_TRACE("generated source/query step=" + std::to_string(step));
        if (source.empty() || step % 4 == 0) {
            gui::TrackRow row;
            row.id = library::PlaylistEntryId{"occurrence-" + std::to_string(next)};
            row.media = {"shared-" + std::to_string(next % 3)};
            row.metadata.title = next % 2 ? "Alpha" : "BETA";
            row.metadata.artist = next % 3 ? "Ensemble" : "Solo";
            row.location = "/music/" + std::to_string(next++) + ".flac";
            row.tags = {
                {{"calm"}, "Mood", "CALM"},
                {{"live"}, "mood", "Live"}
            };
            source.push_back(row);
        } else if (step % 4 == 1) {
            const auto shared = source[step % source.size()].media;
            for (auto& row : source)
                if (row.media == shared) {
                    row.metadata.title = step % 2 ? "ALPHA" : "Beta";
                    row.tags.resize(step % 3 ? 1 : 0);
                }
        } else if (step % 4 == 2 && source.size() > 2) {
            source.erase(source.begin() + step % source.size());
        } else {
            std::rotate(source.begin(), source.begin() + 1, source.end());
        }
        gui::TrackViewState state;
        if (step % 3 == 0)
            state.query.title = "al";
        if (step % 5 == 0)
            state.query.artist = "ensemble";
        if (step % 7 == 0)
            state.query.tags = "calm";
        if (step % 2)
            state.query.filters = {
                {"mood", "calm",
                 step % 11 == 0 ? library::TagFilterMode::exclude
                                   : library::TagFilterMode::include}
            };
        const bool descending = step % 3 == 1;
        state.sort_keys = {
            {.field = gui::TrackSortField::title,
             .direction = descending ? gui::TrackSortDirection::descending
                                        : gui::TrackSortDirection::ascending}
        };
        // This evaluator intentionally does not call the production search/sort pipeline.
        std::vector<gui::TrackRow> expected;
        for (const auto& row : source) {
            if (folded(row.metadata.title).find(state.query.title) == std::string::npos ||
                folded(row.metadata.artist).find(state.query.artist) == std::string::npos)
                continue;
            const bool calm = std::any_of(row.tags.begin(), row.tags.end(), [](const auto& tag) {
                return folded(tag.key) == "mood" && folded(tag.value) == "calm";
            });
            if (!state.query.tags.empty() && !calm)
                continue;
            if (!state.query.filters.empty() &&
                (state.query.filters.front().mode == library::TagFilterMode::include ? !calm
                                                                                     : calm))
                continue;
            expected.push_back(row);
        }
        std::stable_sort(expected.begin(), expected.end(), [&](const auto& a, const auto& b) {
            const auto left = folded(a.metadata.title), right = folded(b.metadata.title);
            if (left != right)
                return descending ? left > right : left < right;
            return a.location.generic_string() < b.location.generic_string();
        });
        model.show_rows(source);
        ASSERT_TRUE(model.set_view_state(state) || model.view_state() == state);
        ASSERT_EQ(model.rowCount(), expected.size());
        EXPECT_FALSE(model.data(QModelIndex{}, Qt::DisplayRole).isValid());
        EXPECT_FALSE(model.row_id(QModelIndex{}));
        EXPECT_EQ(model.rowCount(model.index(0, 0)), 0);
        for (std::size_t row = 0; row < expected.size(); ++row) {
            EXPECT_EQ(model.row_id(model.index(static_cast<int>(row), 1)), expected[row].id);
            EXPECT_EQ(model.index_of(expected[row].id, 1).row(), row);
        }
        if (!expected.empty()) {
            const auto identity = expected.front().id;
            QPersistentModelIndex retained = model.index(0, 1);
            model.set_current(identity);
            EXPECT_TRUE(retained.isValid());
            EXPECT_EQ(model.row_id(retained), identity);
        }
    }
    EXPECT_EQ(resets, 0);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
