/**
 * @file
 * @brief Incremental playlist model regression tests.
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

#include <gui/playlist_model.hpp>

#include <QApplication>

#include <gtest/gtest.h>

TEST(PlaylistModel, ChangesOnlyTheRowWhoseMetadataChanged)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    int argc{1};
    char name[]{"playlist_model_test"};
    char* argv[]{name, nullptr};
    QApplication application{argc, argv};
    QWidget styled;
    wiola::gui::PlaylistModel model{styled};
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("first.wav"), wiola::engine::Track::of("second.wav")});
    model.show_playlist(playlist);

    int changes{0};
    QObject::connect(&model, &QAbstractItemModel::dataChanged,
        [&changes](const QModelIndex& first, const QModelIndex& last, const QList<int>&) {
            EXPECT_EQ(first.row(), last.row());
            ++changes;
        });

    playlist.describe(playlist.tracks()[1].id(),
        wiola::engine::TrackMetadata{.title = "new title",
            .artist = {},
            .album = {},
            .duration = {}});
    model.show_playlist(playlist);

    EXPECT_EQ(changes, 1);
    EXPECT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.data(model.index(1, 1), Qt::DisplayRole).toString(), "new title");
}
