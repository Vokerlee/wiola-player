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

#include <presentation/documents/queue_rows.hpp>

#include <media/local/resource.hpp>

#include <fixtures/track_view.hpp>
#include <qt/widgets/tracks/track_table_view.hpp>

#include <library/in_memory_repository.hpp>
#include <library/location.hpp>
#include <queue/playlist.hpp>

#include <QAction>
#include <QApplication>
#include <QHeaderView>
#include <QTest>

#include <gtest/gtest.h>

#include <algorithm>

namespace {

wiola::engine::QueueEntryId id_at(const wiola::engine::QueueView& playlist, std::size_t row)
{
    return playlist.tracks()[row].id();
}

QAction* action(QWidget& widget, const QString& text)
{
    const QList<QAction*> actions{widget.actions()};
    const auto found = std::ranges::find_if(actions,
        [&text](const QAction* candidate) { return candidate->text() == text; });

    return found == actions.end() ? nullptr : *found;
}

TEST(TrackTableView, HiddenStandardColumnKeepsWidthWhenTagSchemaChanges)
{
    wiola::gui::TrackTableView view;
    view.setColumnWidth(2, 231);
    auto* artist = action(*view.horizontalHeader(), "Artist");
    ASSERT_NE(artist, nullptr);
    artist->setChecked(false);
    wiola::app::TagKeyCatalog catalog;
    wiola::library::TrackRecord record;
    record.id = {"one"};
    record.tags = {
        {.id = {"tag"}, .key = "Genre", .value = "Jazz"}
    };
    catalog.upsert(record);
    view.set_tag_catalog(&catalog);
    wiola::library::InMemoryRepository repository;
    view.show_library(repository);
    auto* genre = action(*view.horizontalHeader(), "Tag: Genre");
    ASSERT_NE(genre, nullptr);
    genre->setChecked(true);
    artist->setChecked(true);
    EXPECT_EQ(view.columnWidth(2), 231);
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
    playlist.set({wiola::media::local_resource("first.wav"),
        wiola::media::local_resource("second.wav")});
    view.show_rows(wiola::gui::queue_rows(playlist.tracks()));
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
    QObject::connect(&view, &wiola::gui::TrackTableView::row_chosen,
        [&chosen](wiola::gui::TrackRowId id) {
            chosen = std::get<wiola::engine::QueueEntryId>(id);
        });
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
    playlist.set({wiola::media::local_resource("first.wav"),
        wiola::media::local_resource("second.wav"), wiola::media::local_resource("third.wav")});
    view.show_rows(wiola::gui::queue_rows(playlist.tracks()));

    view.selectionModel()->select(view.model()->index(0, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    view.selectionModel()->select(view.model()->index(2, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    view.selectionModel()->setCurrentIndex(view.model()->index(2, 0),
        QItemSelectionModel::NoUpdate);

    std::vector<wiola::engine::QueueEntry> reordered{playlist.tracks().begin(),
        playlist.tracks().end()};
    std::ranges::reverse(reordered);
    view.show_rows(wiola::gui::queue_rows(reordered));

    std::vector<wiola::engine::QueueEntryId> selected =
        wiola::gui::queue_entry_ids(view.chosen_rows());
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
    playlist.set({wiola::media::local_resource("first.wav"),
        wiola::media::local_resource("second.wav")});
    view.show_rows(wiola::gui::queue_rows(playlist.tracks()));
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
    EXPECT_EQ(wiola::gui::queue_entry_ids(view.chosen_rows()),
        std::vector<wiola::engine::QueueEntryId>{id_at(playlist, 0)});

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
    EXPECT_EQ(wiola::gui::queue_entry_ids(view.chosen_rows()),
        std::vector<wiola::engine::QueueEntryId>{id_at(playlist, 0)});
}

TEST(TrackTableView, RetainsTagWidthAndOrderWhenHiddenAndReappearing)
{
    wiola::gui::TrackTableView view;
    wiola::app::TagKeyCatalog catalog;
    view.set_tag_catalog(&catalog);
    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord record;
    record.id = {"one"};
    record.tags = {
        {.id = {"tag"}, .key = "Genre", .value = "Jazz"}
    };
    repository.put_track(record);
    catalog.upsert(record);
    view.show_library(repository);
    auto* genre = action(*view.horizontalHeader(), "Tag: Genre");
    ASSERT_NE(genre, nullptr);
    genre->setChecked(true);
    view.setColumnWidth(5, 237);
    view.horizontalHeader()->moveSection(5, 1);
    genre->setChecked(false);
    catalog.remove(record.id);
    view.show_library(repository);
    catalog.upsert(record);
    view.show_library(repository);
    genre = action(*view.horizontalHeader(), "Tag: Genre");
    ASSERT_NE(genre, nullptr);
    genre->setChecked(true);
    EXPECT_EQ(view.columnWidth(5), 237);
    EXPECT_EQ(view.horizontalHeader()->visualIndex(5), 1);
    EXPECT_EQ(wiola::gui::tag_track_column(" Genre ").id, wiola::gui::tag_track_column("genre").id);
}

TEST(TrackTableView, MetadataRefreshKeepsSelectionAndFilteringClearsHiddenSelections)
{
    wiola::gui::TrackTableView view;
    wiola::gui::TrackRow first, second;
    first.id = wiola::library::PlaylistEntryId{"one"};
    first.metadata.title = "Alpha";
    second.id = wiola::library::PlaylistEntryId{"two"};
    second.metadata.title = "Beta";
    view.show_rows({first, second});
    view.selectRow(0);
    int changes = 0;
    QObject::connect(view.selectionModel(), &QItemSelectionModel::selectionChanged,
        [&] { ++changes; });
    first.metadata.artist = "Changed";
    view.show_rows({first, second});
    EXPECT_EQ(changes, 0);
    EXPECT_EQ(view.chosen_rows(), std::vector<wiola::gui::TrackRowId>{first.id});
    wiola::library::SearchQuery query;
    query.title = "beta";
    wiola::testing::set_track_query(view, query);
    EXPECT_TRUE(view.chosen_rows().empty());
    EXPECT_FALSE(view.currentIndex().isValid());
    wiola::testing::set_track_query(view, {});
    EXPECT_TRUE(view.chosen_rows().empty());
}

TEST(TrackTableView, HeaderClicksSortOnlyTheViewAndApplyIsExplicit)
{
    wiola::gui::TrackTableView view;
    wiola::engine::Playlist playlist;
    playlist.set({wiola::media::local_resource("b.wav"), wiola::media::local_resource("a.wav")});
    playlist.describe(id_at(playlist, 0),
        {.title = "Zulu", .artist = {}, .album = {}, .duration = {}});
    playlist.describe(id_at(playlist, 1),
        {.title = "Alpha", .artist = {}, .album = {}, .duration = {}});
    wiola::library::InMemoryRepository repository;
    view.show_library(repository);
    view.show_rows(wiola::gui::queue_rows(playlist.tracks()));

    emit view.horizontalHeader()->sectionClicked(1);
    EXPECT_EQ(wiola::gui::queue_entry_ids(view.displayed_rows()),
        (std::vector<wiola::engine::QueueEntryId>{id_at(playlist, 1), id_at(playlist, 0)}));
    EXPECT_EQ(playlist.tracks()[0].resource(), wiola::media::local_resource("b.wav"));

    emit view.horizontalHeader()->sectionClicked(1);
    EXPECT_EQ(wiola::gui::queue_entry_ids(view.displayed_rows()),
        (std::vector<wiola::engine::QueueEntryId>{id_at(playlist, 0), id_at(playlist, 1)}));

    QAction* apply = action(view, "Apply displayed order to playback");
    ASSERT_NE(apply, nullptr);
    int requests = 0;
    QObject::connect(&view, &wiola::gui::TrackTableView::order_application_asked,
        [&requests] { ++requests; });
    apply->trigger();
    EXPECT_EQ(requests, 1);
    EXPECT_EQ(playlist.tracks()[0].resource(), wiola::media::local_resource("b.wav"));
}

TEST(TrackTableView, HeaderModifiersExtendAndToggleSortKeysWithoutChangingTheQueue)
{
    wiola::gui::TrackTableView view;
    wiola::engine::Playlist playlist;
    playlist.set({wiola::media::local_resource("same.wav"),
        wiola::media::local_resource("same.wav"), wiola::media::local_resource("other.wav")});
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
    wiola::app::TagKeyCatalog catalog;
    catalog.upsert(record);
    view.set_tag_catalog(&catalog);
    view.show_library(repository);
    auto* genre = action(*view.horizontalHeader(), "Tag: Genre");
    ASSERT_NE(genre, nullptr);
    genre->setChecked(true);
    view.show_rows(wiola::gui::queue_rows(playlist.tracks()));
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
    EXPECT_EQ(wiola::gui::queue_entry_ids(view.displayed_rows()),
        (std::vector{id_at(playlist, 2), id_at(playlist, 1), id_at(playlist, 0)}));
    click(2, Qt::ControlModifier);
    EXPECT_EQ(wiola::gui::queue_entry_ids(view.displayed_rows()),
        (std::vector{id_at(playlist, 2), id_at(playlist, 0), id_at(playlist, 1)}));
    click(5, Qt::ShiftModifier);
    EXPECT_EQ(wiola::gui::queue_entry_ids(view.displayed_rows()),
        (std::vector{id_at(playlist, 2), id_at(playlist, 0), id_at(playlist, 1)}));
    EXPECT_EQ(wiola::gui::queue_entry_ids(view.chosen_rows()), std::vector{id_at(playlist, 1)});
    EXPECT_EQ(playlist.tracks()[0].id(), id_at(playlist, 0));
    EXPECT_EQ(playlist.tracks()[1].resource(), wiola::media::local_resource("same.wav"));
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
