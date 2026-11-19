/**
 * @file
 * @brief Now Playing document regression tests.
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

#include <gui/now_playing_document.hpp>
#include <gui/track_table_view.hpp>

#include <QAction>
#include <QApplication>
#include <QFont>
#include <QLabel>

#include <gtest/gtest.h>

#include <algorithm>

namespace {

QAction* action(QWidget& widget, const QString& text)
{
    const QList<QAction*> actions{widget.actions()};
    const auto found = std::ranges::find_if(actions,
        [&text](const QAction* candidate) { return candidate->text() == text; });

    return found == actions.end() ? nullptr : *found;
}

TEST(NowPlayingDocument, PresentsTheActualEngineQueueAndCurrentTrack)
{
    wiola::gui::NowPlayingDocument document;
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("first.wav"), wiola::engine::Track::of("second.wav")});
    playlist.go_to(1);

    document.show_playlist(playlist);

    QLabel* heading = document.findChild<QLabel*>("nowPlayingHeading");
    wiola::gui::TrackTableView* table = document.findChild<wiola::gui::TrackTableView*>();
    ASSERT_NE(heading, nullptr);
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 2);

    EXPECT_EQ(heading->text(), "Now Playing");
    EXPECT_EQ(table->accessibleName(), "Now Playing tracks");
    EXPECT_FALSE(
        table->model()->data(table->model()->index(0, 1), Qt::FontRole).value<QFont>().bold()
    );
    EXPECT_TRUE(
        table->model()->data(table->model()->index(1, 1), Qt::FontRole).value<QFont>().bold()
    );
}

TEST(NowPlayingDocument, ForwardsIdentityBasedCommandsFromItsTable)
{
    wiola::gui::NowPlayingDocument document;
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("first.wav"), wiola::engine::Track::of("second.wav")});
    document.show_playlist(playlist);

    wiola::gui::TrackTableView* table = document.findChild<wiola::gui::TrackTableView*>();
    ASSERT_NE(table, nullptr);

    table->selectionModel()->select(table->model()->index(1, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    table->selectionModel()->setCurrentIndex(table->model()->index(1, 0),
        QItemSelectionModel::NoUpdate);

    std::optional<wiola::engine::QueueEntryId> chosen;
    int removals{0};
    QObject::connect(&document, &wiola::gui::NowPlayingDocument::track_chosen,
        [&chosen](wiola::engine::QueueEntryId id) { chosen = id; });
    QObject::connect(&document, &wiola::gui::NowPlayingDocument::removal_asked,
        [&removals] { ++removals; });

    QAction* play = action(*table, "Play selected track");
    QAction* remove = action(*table, "Remove selected tracks");
    ASSERT_NE(play, nullptr);
    ASSERT_NE(remove, nullptr);

    play->trigger();
    remove->trigger();

    const auto selected = playlist.tracks()[1].id();
    EXPECT_EQ(chosen, selected);
    EXPECT_EQ(document.chosen_tracks(), std::vector<wiola::engine::QueueEntryId>{selected});
    EXPECT_EQ(removals, 1);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
