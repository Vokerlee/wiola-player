/**
 * @file
 * @brief Toolkit-independent document session restoration contracts.
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

#include <gtest/gtest.h>
#include <wiola/presentation/documents/session_documents.hpp>

namespace {

using namespace wiola;

TEST(SessionDocuments, QueueSelectionCurrentAndAnchorUseTheNewOccurrenceIdentities)
{
    gui::SessionDocuments state;
    auto& browser = state.browsers[gui::DocumentId::queue()];
    browser.selected = {engine::QueueEntryId{5}, engine::QueueEntryId{6}};
    browser.current = engine::QueueEntryId{5};
    browser.anchor = engine::QueueEntryId{6};
    browser.anchor_offset = 17;

    state.remap_queue({
        {engine::QueueEntryId{5}, engine::QueueEntryId{50}}
    });

    EXPECT_EQ(browser.selected, (std::vector<gui::TrackRowId>{engine::QueueEntryId{50}}));
    EXPECT_EQ(browser.current, gui::TrackRowId{engine::QueueEntryId{50}});
    EXPECT_FALSE(browser.anchor);
    EXPECT_EQ(browser.anchor_offset, 17);
}

TEST(SessionDocuments, RemappingPreservesOtherDocumentIdentitiesAndNavigation)
{
    gui::SessionDocuments state;
    const auto playlist = gui::DocumentId::playlist(library::PlaylistId{"saved"});
    const auto workspace = gui::DocumentId::workspace(library::WorkspaceId{"folder"});
    state.open = {gui::DocumentId::queue(), playlist, workspace};
    state.visible = workspace;
    state.destination = library::PlaylistId{"destination"};
    const gui::TrackRowId saved = library::PlaylistEntryId{"saved-occurrence"};
    const gui::TrackRowId scanned = gui::WorkspaceTrackId{{"folder"}, {"media"}};
    auto& saved_browser = state.browsers[playlist];
    saved_browser.selected = {saved};
    saved_browser.current = saved;
    saved_browser.anchor = saved;
    auto& scanned_browser = state.browsers[workspace];
    scanned_browser.selected = {scanned};
    scanned_browser.current = scanned;
    scanned_browser.anchor = scanned;
    const auto before = state;

    state.remap_queue({
        {engine::QueueEntryId{5}, engine::QueueEntryId{50}}
    });

    EXPECT_EQ(state, before);
}

TEST(SessionDocuments, AnEmptyMappingDropsOnlyQueueOccurrences)
{
    gui::SessionDocuments state;
    const gui::TrackRowId saved = library::PlaylistEntryId{"saved-occurrence"};
    auto& browser = state.browsers[gui::DocumentId::queue()];
    browser.selected = {engine::QueueEntryId{5}, saved, engine::QueueEntryId{6}};
    browser.current = engine::QueueEntryId{5};
    browser.anchor = saved;

    state.remap_queue({});

    EXPECT_EQ(browser.selected, (std::vector{saved}));
    EXPECT_FALSE(browser.current);
    EXPECT_EQ(browser.anchor, saved);
}

TEST(SessionDocuments, EmptyOptionalSelectionsRemainEmptyInAnOwnedSnapshot)
{
    gui::SessionDocuments state;
    state.browsers.emplace(gui::DocumentId::queue(), gui::BrowserViewState{});
    const auto before = state;

    state.remap_queue({
        {engine::QueueEntryId{5}, engine::QueueEntryId{50}}
    });

    EXPECT_EQ(state, before);
}

} // namespace
