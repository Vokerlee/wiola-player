/**
 * @file
 * @brief Toolkit-independent open documents, destination and browser restoration.
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

#pragma once

#include "document_id.hpp"
#include <map>
#include <optional>
#include <vector>
#include <wiola/presentation/tracks/browser_view_state.hpp>

namespace wiola::gui {

struct SessionDocuments {
    std::vector<DocumentId> open{DocumentId::queue()};
    std::optional<DocumentId> visible{DocumentId::queue()};
    std::optional<library::PlaylistId> destination;
    std::map<DocumentId, BrowserViewState> browsers;

    void remap_queue(const std::map<engine::QueueEntryId, engine::QueueEntryId>& mapping);
    bool operator==(const SessionDocuments&) const = default;
};

} // namespace wiola::gui
