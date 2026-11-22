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
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <array>
#include <gtest/gtest.h>
#include <gui/tracks/track_browser.hpp>

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
        auto* move = action(browser, "Move to playlist →");
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

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "minimal");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};
    return RUN_ALL_TESTS();
}
