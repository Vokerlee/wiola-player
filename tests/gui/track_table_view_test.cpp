/**
 * @file
 * @brief Reusable track table view regression tests.
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

#include <gui/track_table_view.hpp>

#include <engine/queue/playlist.hpp>

#include <QAction>
#include <QApplication>
#include <QHeaderView>

#include <gtest/gtest.h>

#include <algorithm>

namespace {

wiola::engine::QueueEntryId id_at(const wiola::engine::Playlist& playlist, std::size_t row)
{
    return playlist.tracks().at(row).id();
}

QAction* action(QWidget& widget, const QString& text)
{
    const QList<QAction*> actions{widget.actions()};
    const auto found = std::ranges::find_if(actions,
        [&text](const QAction* candidate) { return candidate->text() == text; });

    return found == actions.end() ? nullptr : *found;
}

TEST(TrackTableView, OffersColumnVisibilityControls)
{
    wiola::gui::TrackTableView view;
    QAction* title = action(*view.horizontalHeader(), "Title");
    QAction* artist = action(*view.horizontalHeader(), "Artist");
    QAction* album = action(*view.horizontalHeader(), "Album");
    QAction* length = action(*view.horizontalHeader(), "Length");

    ASSERT_NE(title, nullptr);
    ASSERT_NE(artist, nullptr);
    ASSERT_NE(album, nullptr);
    ASSERT_NE(length, nullptr);

    title->setChecked(false);
    EXPECT_TRUE(view.isColumnHidden(1));

    title->setChecked(true);
    EXPECT_FALSE(view.isColumnHidden(1));
    EXPECT_EQ(view.contextMenuPolicy(), Qt::ActionsContextMenu);
    EXPECT_EQ(view.horizontalHeader()->contextMenuPolicy(), Qt::ActionsContextMenu);
}

TEST(TrackTableView, ProvidesKeyboardFocusAndContextCommands)
{
    wiola::gui::TrackTableView view;
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("first.wav"), wiola::engine::Track::of("second.wav")});
    view.show_tracks(playlist.tracks());
    view.setCurrentIndex(view.model()->index(1, 0));

    QAction* play = action(view, "Play selected track");
    QAction* remove = action(view, "Remove selected tracks");
    ASSERT_NE(play, nullptr);
    ASSERT_NE(remove, nullptr);

    std::optional<wiola::engine::QueueEntryId> chosen;
    int removals{0};
    QObject::connect(&view, &wiola::gui::TrackTableView::track_chosen,
        [&chosen](wiola::engine::QueueEntryId id) { chosen = id; });
    QObject::connect(&view, &wiola::gui::TrackTableView::removal_asked,
        [&removals] { ++removals; });

    play->trigger();
    remove->trigger();

    EXPECT_EQ(chosen, id_at(playlist, 1));
    EXPECT_EQ(removals, 1);
    EXPECT_EQ(view.focusPolicy(), Qt::StrongFocus);
    EXPECT_EQ(view.accessibleName(), "Track list");
}

TEST(TrackTableView, RestoresSelectionAndKeyboardPositionByTrackIdentity)
{
    wiola::gui::TrackTableView view;
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("first.wav"), wiola::engine::Track::of("second.wav"),
        wiola::engine::Track::of("third.wav")});
    view.show_tracks(playlist.tracks());

    view.selectionModel()->select(view.model()->index(0, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    view.selectionModel()->select(view.model()->index(2, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    view.selectionModel()->setCurrentIndex(view.model()->index(2, 0),
        QItemSelectionModel::NoUpdate);

    std::vector<wiola::engine::Track> reordered{playlist.tracks()};
    std::ranges::reverse(reordered);
    view.show_tracks(reordered);

    std::vector<wiola::engine::QueueEntryId> selected = view.chosen_tracks();
    std::ranges::sort(selected, {}, &wiola::engine::QueueEntryId::value);

    ASSERT_EQ(selected.size(), 2);
    EXPECT_EQ(selected[0], id_at(playlist, 0));
    EXPECT_EQ(selected[1], id_at(playlist, 2));
    EXPECT_EQ(view.currentIndex().row(), 0);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
