/**
 * @file
 * @brief Occurrence remapping for portable document session restoration.
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

#include "session_documents.hpp"

#include <utility>

namespace wiola::gui {

void SessionDocuments::remap_queue(
    const std::map<engine::QueueEntryId, engine::QueueEntryId>& mapping
)
{
    for (auto& [id, browser] : browsers) {
        const auto remap = [&](std::optional<TrackRowId> value) -> std::optional<TrackRowId> {
            if (!value)
                return {};

            if (const auto* queue = std::get_if<engine::QueueEntryId>(&*value)) {
                const auto found = mapping.find(*queue);
                return found == mapping.end() ? std::nullopt
                                              : std::optional<TrackRowId>{found->second};
            }

            return value;
        };
        std::vector<TrackRowId> selected;

        for (const auto& occurrence : browser.selected)
            if (auto mapped = remap(occurrence))
                selected.push_back(*mapped);

        browser.selected = std::move(selected);
        browser.current = remap(browser.current);
        browser.anchor = remap(browser.anchor);
    }
}

} // namespace wiola::gui
