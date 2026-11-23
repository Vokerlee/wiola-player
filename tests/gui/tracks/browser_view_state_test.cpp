/**
 * @file
 * @brief Shared track browsing composition with explicit command capabilities.
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

#include <QAction>
#include <QApplication>
#include <QHeaderView>
#include <QTest>
#include <gtest/gtest.h>
#include <gui/tracks/track_browser.hpp>
#include <library/in_memory_repository.hpp>

namespace {

using namespace wiola;

gui::TrackRow row(const char* occurrence, const char* title = "Song")
{
    gui::TrackRow row;
    row.id = library::PlaylistEntryId{occurrence};
    row.media = library::MediaId{"same-media"};
    row.metadata.title = title;
    row.metadata.artist = "Artist";
    row.metadata.album = "Album";
    row.tags = {
        {.id = library::TagId{"tag"}, .key = "Mood", .value = "Calm"}
    };
    return row;
}

QAction* action(gui::TrackTableView& table, const QString& text)
{
    for (auto* action : table.horizontalHeader()->actions())
        if (action->text() == text)
            return action;
    return nullptr;
}

TEST(BrowserViewState, DeferredSelectionRestoresWhenItsOccurrenceArrivesAndNewUserIntentCancelsIt)
{
    gui::TrackTableView table;
    table.resize(600, 300);
    table.show();
    QApplication::processEvents();
    app::BrowserViewState saved;
    saved.selected = {library::PlaylistEntryId{"second"}};
    saved.current = saved.selected.front();
    table.restore_browser_state(saved);
    table.show_rows({row("first")});
    EXPECT_TRUE(table.chosen_rows().empty());
    EXPECT_EQ(table.capture_browser_state().selected, saved.selected);
    table.show_rows({row("first"), row("second")});
    EXPECT_EQ(table.chosen_rows(), saved.selected);
    EXPECT_EQ(table.capture_browser_state().current, saved.current);
    table.show_rows({row("first")});
    table.restore_browser_state(saved);
    const auto first = table.model()->index(0, 1);
    QTest::mouseClick(table.viewport(), Qt::LeftButton, Qt::NoModifier,
        table.visualRect(first).center());
    table.show_rows({row("first"), row("second")});
    EXPECT_EQ(table.chosen_rows(),
        (std::vector<gui::TrackRowId>{library::PlaylistEntryId{"first"}}));
}

TEST(BrowserViewState, RestoresColumnOrderWidthsVisibilityAndMatchingMenuChecks)
{
    app::TagKeyCatalog catalog;
    library::TrackRecord track;
    track.id = library::MediaId{"same-media"};
    track.tags = row("first").tags;
    catalog.upsert(track);
    library::InMemoryRepository repository;
    ASSERT_TRUE(repository.put_track(track));
    gui::TrackTableView first;
    first.set_tag_catalog(&catalog);
    first.show_rows({row("first")});
    first.show_library(repository);
    ASSERT_NE(action(first, "Tag: Mood"), nullptr);
    action(first, "Tag: Mood")->setChecked(true);
    first.setColumnWidth(gui::StandardTrackColumn::artist, 211);
    action(first, "Artist")->setChecked(false);
    const int custom = first.model()->columnCount() - 1;
    first.setColumnWidth(custom, 233);
    first.horizontalHeader()->moveSection(first.horizontalHeader()->visualIndex(custom), 1);
    const auto saved = first.capture_browser_state();
    gui::TrackTableView second;
    second.set_tag_catalog(&catalog);
    second.show_rows({row("first")});
    second.show_library(repository);
    second.restore_browser_state(saved);
    EXPECT_TRUE(second.isColumnHidden(gui::StandardTrackColumn::artist));
    ASSERT_NE(action(second, "Artist"), nullptr);
    EXPECT_FALSE(action(second, "Artist")->isChecked());
    ASSERT_NE(action(second, "Tag: Mood"), nullptr);
    EXPECT_TRUE(action(second, "Tag: Mood")->isChecked());
    const int restored = second.model()->columnCount() - 1;
    EXPECT_EQ(second.columnWidth(restored), 233);
    EXPECT_EQ(second.horizontalHeader()->visualIndex(restored), 1);
    action(second, "Artist")->setChecked(true);
    EXPECT_EQ(second.columnWidth(gui::StandardTrackColumn::artist), 211);
}

TEST(BrowserViewState, QueryPanelsSortAndDuplicateOccurrenceSelectionRestoreTogether)
{
    gui::TrackBrowser browser{gui::TrackCapabilities::playlist()};
    browser.show_rows({row("first"), row("second")});
    app::BrowserViewState state;
    state.view.query = {.title = "Song", .artist = "Artist", .album = "Album", .tags = "Calm"};
    state.view.sort_keys = {
        {.field = gui::TrackSortField::artist, .direction = gui::TrackSortDirection::descending}
    };
    state.selected = {library::PlaylistEntryId{"second"}};
    state.current = state.selected.front();
    browser.restore_browser_state(state);
    browser.table().finish_browser_restore();
    EXPECT_EQ(browser.table().chosen_rows(), state.selected);
    EXPECT_EQ(browser.findChild<QLineEdit*>("searchTitle")->text(), "song");
    EXPECT_EQ(browser.findChild<QLineEdit*>("searchArtist")->text(), "artist");
    EXPECT_EQ(browser.findChild<QLineEdit*>("searchAlbum")->text(), "album");
    EXPECT_EQ(browser.view_state().sort_keys, state.view.sort_keys);
    EXPECT_EQ(browser.capture_browser_state().current, state.current);
}

} // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "minimal");
    QApplication application{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
