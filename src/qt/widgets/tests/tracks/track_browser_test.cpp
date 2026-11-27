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

#include <app/library/search_index.hpp>
#include <qt/widgets/tracks/track_browser.hpp>

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <gtest/gtest.h>

#include <array>

namespace {

using namespace wiola::gui;

QAction* action(TrackBrowser& browser, const QString& text)
{
    for (auto* action : browser.table().actions())
        if (action->text() == text)
            return action;
    return nullptr;
}

TEST(TrackBrowser, SharesSearchAndResultCountsAcrossAllDocumentContexts)
{
    const std::array contexts{TrackCapabilities::queue(), TrackCapabilities::playlist(),
        TrackCapabilities::workspace()};
    for (const auto capabilities : contexts) {
        TrackBrowser browser{capabilities};
        TrackRow first, second;
        first.id = wiola::library::PlaylistEntryId{"first"};
        first.media = {"first"};
        first.metadata.title = "Alpha";
        first.metadata.artist = "Artist";
        second = first;
        second.id = wiola::library::PlaylistEntryId{"second"};
        second.media = {"second"};
        second.metadata.title = "Beta";
        browser.show_rows({first, second});

        auto* title = browser.findChild<QLineEdit*>("searchTitle");
        auto* artist = browser.findChild<QLineEdit*>("searchArtist");
        auto* results = browser.findChild<QLabel*>("trackSearchResults");

        ASSERT_NE(title, nullptr);
        ASSERT_NE(artist, nullptr);
        ASSERT_NE(results, nullptr);
        EXPECT_EQ(results->text(), "2 of 2 tracks");

        title->setText("alpha");
        EXPECT_EQ(results->text(), "1 of 2 tracks");

        artist->setText("Other");
        EXPECT_EQ(results->text(), "0 of 2 tracks");

        artist->clear();
        EXPECT_EQ(browser.table().visible_count(), 1);
    }
}

TEST(TrackBrowser, CapabilitiesGovernMenusAndKeyboardCommands)
{
    const std::array contexts{TrackCapabilities::queue(), TrackCapabilities::playlist(),
        TrackCapabilities::workspace()};
    for (const auto capabilities : contexts) {
        TrackBrowser browser{capabilities};
        auto* move = action(browser, "Move to playlist");
        auto* remove = action(browser, "Remove selected tracks");

        ASSERT_NE(move, nullptr);
        ASSERT_NE(remove, nullptr);
        EXPECT_EQ(move->isVisible(), capabilities.supports(TrackCommand::move));
        EXPECT_EQ(remove->isEnabled(), capabilities.supports(TrackCommand::remove));

        int removals = 0;
        QObject::connect(&browser, &TrackBrowser::command_requested, [&](TrackCommand command) {
            if (command == TrackCommand::remove)
                ++removals;
        });

        QKeyEvent key{QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier};
        QApplication::sendEvent(&browser.table(), &key);
        EXPECT_EQ(removals, capabilities.supports(TrackCommand::remove) ? 1 : 0);
    }
}

TEST(TrackBrowser, QueueAppendFollowsCapabilitiesSelectionAndRestoration)
{
    for (const auto capabilities : {TrackCapabilities::queue(), TrackCapabilities::playlist(),
             TrackCapabilities::workspace()}) {
        TrackBrowser browser{capabilities};
        auto* append = action(browser, "Add to play queue");

        ASSERT_NE(append, nullptr);
        EXPECT_EQ(append->isVisible(), capabilities.supports(TrackCommand::append));
        EXPECT_FALSE(append->isEnabled());

        TrackRow row;
        row.id = wiola::library::PlaylistEntryId{"selected"};
        row.media = {"track"};
        row.metadata.title = "Selected";
        browser.show_rows({row});
        browser.table().selectRow(0);

        const bool supported = capabilities.supports(TrackCommand::append);

        EXPECT_EQ(append->isEnabled(), supported);

        int requests{};
        QObject::connect(&browser, &TrackBrowser::command_requested, [&](TrackCommand command) {
            EXPECT_EQ(command, TrackCommand::append);
            ++requests;
        });

        append->trigger();

        EXPECT_EQ(requests, supported ? 1 : 0);

        const auto saved = browser.capture_browser_state();
        browser.table().clearSelection();

        EXPECT_FALSE(append->isEnabled());

        browser.restore_browser_state(saved);
        browser.table().finish_browser_restore();

        EXPECT_EQ(append->isEnabled(), supported);

        browser.table().set_capabilities(TrackCapabilities{});

        EXPECT_FALSE(append->isVisible());
        EXPECT_FALSE(append->isEnabled());
    }
}

TEST(TrackBrowser, EditCommandsFollowRestoredSelectionAcrossDocumentReloads)
{
    for (const auto capabilities : {TrackCapabilities::queue(), TrackCapabilities::playlist(),
             TrackCapabilities::workspace()}) {
        TrackRow row;
        row.id = wiola::library::PlaylistEntryId{"selected"};
        row.media = {"track"};
        row.metadata.title = "Alpha";

        TrackBrowser browser{capabilities};
        browser.show_rows({row});
        browser.table().selectRow(0);

        const auto saved = browser.capture_browser_state();
        auto* button = browser.findChild<QPushButton*>("editSelectedTrack");
        auto* context_edit = action(browser, "Edit track details…");

        ASSERT_NE(button, nullptr);
        ASSERT_NE(context_edit, nullptr);
        ASSERT_TRUE(button->isEnabled());

        browser.table().clearSelection();
        ASSERT_FALSE(button->isEnabled());
        ASSERT_FALSE(context_edit->isEnabled());

        browser.restore_browser_state(saved);
        browser.table().finish_browser_restore();
        ASSERT_EQ(browser.table().chosen_rows(), (std::vector<TrackRowId>{row.id}));
        EXPECT_TRUE(button->isEnabled());
        EXPECT_TRUE(context_edit->isEnabled());

        int edits{};
        QObject::connect(&browser, &TrackBrowser::edit_requested, [&] { ++edits; });

        button->click();
        EXPECT_EQ(edits, 1);

        context_edit->trigger();
        EXPECT_EQ(edits, 2);

        browser.show_rows({});
        EXPECT_FALSE(button->isEnabled());
        EXPECT_FALSE(context_edit->isEnabled());

        browser.restore_browser_state(saved);
        browser.show_rows({row});
        browser.table().finish_browser_restore();
        EXPECT_TRUE(button->isEnabled());
        EXPECT_TRUE(context_edit->isEnabled());
    }
}

TEST(TrackBrowser, ReparentedQueryControlsDoNotRetainCallbacksToADestroyedBrowser)
{
    QWidget external;
    auto browser = std::make_unique<TrackBrowser>(TrackCapabilities::queue());
    auto* search = browser->findChild<QWidget*>("trackSearchPanel");

    ASSERT_NE(search, nullptr);

    search->setParent(&external);

    auto* title = search->findChild<QLineEdit*>("searchTitle");

    ASSERT_NE(title, nullptr);

    browser.reset();
    title->setText("retained control");
    EXPECT_EQ(title->text(), "retained control");
}

TEST(TrackBrowser, NormalizesOnceAndIgnoresEquivalentStateUpdates)
{
    TrackBrowser first{TrackCapabilities::queue()}, second{TrackCapabilities::queue()};
    TrackRow row;
    row.id = wiola::library::PlaylistEntryId{"one"};
    row.metadata.title = "Alpha";
    first.show_rows({row});
    first.table().selectRow(0);

    TrackViewState state;
    state.query.title = "  ALPHA  ";
    state.query.filters = {
        {" Genre ", " JAZZ "},
        {"genre",   "jazz"  }
    };
    state.sort_keys = {TrackSortKey::for_tag(" Genre "), TrackSortKey::for_tag("genre")};

    auto normalized = normalized_track_view_state(state);

    EXPECT_EQ(normalized.query.title, "alpha");
    ASSERT_EQ(normalized.query.filters.size(), 1);
    ASSERT_EQ(normalized.sort_keys.size(), 1);
    EXPECT_EQ(normalized.sort_keys.front().tag_key, "genre");

    state.query.filters.clear();
    first.set_view_state(state);

    int resets = 0, changes = 0;
    QObject::connect(first.table().model(), &QAbstractItemModel::modelReset, [&] { ++resets; });
    QObject::connect(&first.table(), &TrackTableView::view_state_changed, [&] { ++changes; });

    first.set_view_state(state);
    EXPECT_EQ(resets, 0);
    EXPECT_EQ(changes, 0);
    EXPECT_EQ(first.table().chosen_rows(), std::vector<TrackRowId>{row.id});
    EXPECT_TRUE(second.view_state().query.title.empty());
}

TEST(TrackBrowser, IndexedSearchUsesCanonicalMediaAndPreservesOccurrenceDuplicates)
{
    wiola::app::SearchIndex index;
    wiola::library::TrackRecord record;
    record.id = {"canonical"};
    record.embedded.title = "Library title";
    record.embedded.artist = "Artist";
    record.tags = {
        {.id = {"tag"}, .key = "Mood", .value = "Calm"}
    };
    index.upsert(record);

    TrackRow first, second;
    first.id = wiola::library::PlaylistEntryId{"one"};
    first.media = record.id;
    first.metadata.title = "Stale row title";
    second = first;
    second.id = wiola::library::PlaylistEntryId{"two"};

    TrackBrowser browser{TrackCapabilities::playlist()};
    browser.table().set_search_index(&index);
    browser.show_rows({second, first});
    browser.findChild<QLineEdit*>("searchTitle")->setText("library");
    browser.findChild<QLineEdit*>("searchArtist")->setText("artist");
    browser.findChild<QLineEdit*>("searchTags")->setText("calm");
    EXPECT_EQ(browser.table().displayed_rows(), (std::vector<TrackRowId>{second.id, first.id}));

    record.tags.front().value = "Loud";
    index.upsert(record);
    browser.show_rows({second, first});
    EXPECT_EQ(browser.table().visible_count(), 0);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "minimal");
    testing::InitGoogleTest(&argc, argv);

    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
