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
#include <QFont>

#include <gtest/gtest.h>

namespace {

class TrackTableModelTest : public testing::Test {
protected:
    QWidget styled;
};

wiola::engine::QueueEntryId id_at(const wiola::engine::Playlist& playlist, std::size_t row)
{
    return playlist.tracks().at(row).id();
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

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
