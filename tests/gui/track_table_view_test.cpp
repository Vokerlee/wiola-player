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
#include <library/in_memory_repository.hpp>
#include <library/location.hpp>

#include <QAction>
#include <QApplication>
#include <QHeaderView>
#include <QTest>

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
    QAction* add_active = action(view, "Add to active playlist");
    QAction* add_to = action(view, "Add to playlist →");
    QAction* move_to = action(view, "Move to playlist →");
    ASSERT_NE(play, nullptr);
    ASSERT_NE(remove, nullptr);
    ASSERT_NE(add_active, nullptr);
    ASSERT_NE(add_to, nullptr);
    ASSERT_NE(move_to, nullptr);

    std::optional<wiola::engine::QueueEntryId> chosen;
    int removals{0};
    int active_transfers{0};
    int destination_transfers{0};
    int moves{0};
    QObject::connect(&view, &wiola::gui::TrackTableView::track_chosen,
        [&chosen](wiola::engine::QueueEntryId id) { chosen = id; });
    QObject::connect(&view, &wiola::gui::TrackTableView::removal_asked,
        [&removals] { ++removals; });
    QObject::connect(&view, &wiola::gui::TrackTableView::transfer_active_asked,
        [&active_transfers] { ++active_transfers; });
    QObject::connect(&view, &wiola::gui::TrackTableView::transfer_destination_asked,
        [&destination_transfers] { ++destination_transfers; });
    QObject::connect(&view, &wiola::gui::TrackTableView::transfer_move_asked,
        [&moves] { ++moves; });

    play->trigger();
    remove->trigger();
    add_active->trigger();
    add_to->trigger();
    move_to->trigger();

    EXPECT_EQ(chosen, id_at(playlist, 1));
    EXPECT_EQ(removals, 1);
    EXPECT_EQ(active_transfers, 1);
    EXPECT_EQ(destination_transfers, 1);
    EXPECT_EQ(moves, 1);
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

TEST(TrackTableView, AddsTagColumnsWithoutLosingSelectionAndUpdatesTheirValues)
{
    wiola::gui::TrackTableView view;
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("first.wav"), wiola::engine::Track::of("second.wav")});
    view.show_tracks(playlist.tracks());
    view.selectionModel()->select(view.model()->index(0, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);

    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord record;
    record.id = wiola::library::media_id_for("first.wav");
    record.tags = {
        {.id = wiola::library::TagId{"tag-1"}, .key = "Genre", .value = "Jazz"}
    };
    ASSERT_TRUE(repository.put_track(record));

    view.show_library(repository);
    QAction* genre = action(*view.horizontalHeader(), "Tag: Genre");
    ASSERT_NE(genre, nullptr);

    genre->setChecked(true);
    EXPECT_EQ(view.model()->columnCount(), 6);
    EXPECT_EQ(view.model()->data(view.model()->index(0, 5), Qt::DisplayRole).toString(), "Jazz");
    EXPECT_EQ(view.chosen_tracks(), std::vector<wiola::engine::QueueEntryId>{id_at(playlist, 0)});

    record.tags.front().value = "Fusion";
    ++record.metadata_revision;
    ASSERT_TRUE(repository.put_track(record));
    ASSERT_TRUE(repository.add_tag(record.id, "Mood", "Calm").valid());
    view.show_library(repository);

    EXPECT_NE(action(*view.horizontalHeader(), "Tag: Mood"), nullptr);
    EXPECT_EQ(view.model()->data(view.model()->index(0, 5), Qt::DisplayRole).toString(), "Fusion");

    genre = action(*view.horizontalHeader(), "Tag: Genre");
    ASSERT_NE(genre, nullptr);
    genre->setChecked(false);
    EXPECT_EQ(view.model()->columnCount(), 5);
    EXPECT_EQ(view.chosen_tracks(), std::vector<wiola::engine::QueueEntryId>{id_at(playlist, 0)});
}

TEST(TrackTableView, HeaderClicksSortOnlyTheViewAndApplyIsExplicit)
{
    wiola::gui::TrackTableView view;
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("b.wav"), wiola::engine::Track::of("a.wav")});
    playlist.describe(id_at(playlist, 0),
        {.title = "Zulu", .artist = {}, .album = {}, .duration = {}});
    playlist.describe(id_at(playlist, 1),
        {.title = "Alpha", .artist = {}, .album = {}, .duration = {}});
    wiola::library::InMemoryRepository repository;
    view.show_library(repository);
    view.show_tracks(playlist.tracks());

    emit view.horizontalHeader()->sectionClicked(1);
    EXPECT_EQ(view.displayed_tracks(),
        (std::vector<wiola::engine::QueueEntryId>{id_at(playlist, 1), id_at(playlist, 0)}));
    EXPECT_EQ(playlist.tracks()[0].path(), "b.wav");

    emit view.horizontalHeader()->sectionClicked(1);
    EXPECT_EQ(view.displayed_tracks(),
        (std::vector<wiola::engine::QueueEntryId>{id_at(playlist, 0), id_at(playlist, 1)}));

    QAction* apply = action(view, "Apply displayed order to playback");
    ASSERT_NE(apply, nullptr);
    int requests = 0;
    QObject::connect(&view, &wiola::gui::TrackTableView::order_application_asked,
        [&requests] { ++requests; });
    apply->trigger();
    EXPECT_EQ(requests, 1);
    EXPECT_EQ(playlist.tracks()[0].path(), "b.wav");
}

TEST(TrackTableView, HeaderModifiersExtendAndToggleSortKeysWithoutChangingTheQueue)
{
    wiola::gui::TrackTableView view;
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("same.wav"), wiola::engine::Track::of("same.wav"),
        wiola::engine::Track::of("other.wav")});
    playlist.describe(id_at(playlist, 0),
        {.title = "Same", .artist = "Zulu", .album = {}, .duration = {}});
    playlist.describe(id_at(playlist, 1),
        {.title = "Same", .artist = "Alpha", .album = {}, .duration = {}});
    playlist.describe(id_at(playlist, 2),
        {.title = "Last", .artist = "Beta", .album = {}, .duration = {}});
    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord record;
    record.id = wiola::library::media_id_for("same.wav");
    record.tags = {
        {.id = wiola::library::TagId{"tag-1"}, .key = "Genre", .value = "Jazz"}
    };
    ASSERT_TRUE(repository.put_track(record));
    view.show_library(repository);
    auto* genre = action(*view.horizontalHeader(), "Tag: Genre");
    ASSERT_NE(genre, nullptr);
    genre->setChecked(true);
    view.show_tracks(playlist.tracks());
    view.selectRow(1);
    view.resize(1000, 400);
    view.show();
    QApplication::processEvents();
    const auto click = [&view](int column, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        auto* header = view.horizontalHeader();
        const QPoint point{header->sectionViewportPosition(column) +
                header->sectionSize(column) / 2,
            header->height() / 2};
        QTest::mouseClick(header->viewport(), Qt::LeftButton, modifiers, point);
    };
    click(1);
    click(2, Qt::ShiftModifier);
    EXPECT_EQ(view.displayed_tracks(),
        (std::vector{id_at(playlist, 2), id_at(playlist, 1), id_at(playlist, 0)}));
    click(2, Qt::ControlModifier);
    EXPECT_EQ(view.displayed_tracks(),
        (std::vector{id_at(playlist, 2), id_at(playlist, 0), id_at(playlist, 1)}));
    click(5, Qt::ShiftModifier);
    EXPECT_EQ(view.displayed_tracks(),
        (std::vector{id_at(playlist, 2), id_at(playlist, 0), id_at(playlist, 1)}));
    EXPECT_EQ(view.chosen_tracks(), std::vector{id_at(playlist, 1)});
    EXPECT_EQ(playlist.tracks()[0].id(), id_at(playlist, 0));
    EXPECT_EQ(playlist.tracks()[1].path(), "same.wav");
}

} // namespace

int main(int argc, char** argv)
{
#ifdef WIOLA_TEST_TSAN
    // Avoid native font discovery threads in the uninstrumented system Qt build.
    qputenv("QT_QPA_PLATFORM", "minimal");
#else
    qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
