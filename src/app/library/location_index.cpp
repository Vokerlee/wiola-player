/**
 * @file
 * @brief Incremental canonical media lookup by normalized location.
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

#include "location_index.hpp"
#include <library/location.hpp>

namespace wiola::app {

void LocationIndex::remove(const library::MediaId& id)
{
    const auto found = media_.find(id);
    if (found == media_.end())
        return;
    for (const auto& key : found->second) {
        auto item = locations_.find(key);
        item->second.erase(id);
        if (item->second.empty())
            locations_.erase(item);
    }
    media_.erase(found);
}

void LocationIndex::upsert(const library::TrackRecord& record)
{
    if (!record.id.valid())
        return;
    remove(record.id);
    auto& keys = media_[record.id];
    for (const auto& path : record.locations)
        if (!path.empty())
            keys.insert(library::media_id_for(path));
    for (const auto& key : keys)
        locations_[key].insert(record.id);
}

std::optional<library::MediaId> LocationIndex::find(const std::filesystem::path& path) const
{
    const auto found = locations_.find(library::media_id_for(path));
    return found == locations_.end() ? std::nullopt : std::optional{*found->second.begin()};
}

} // namespace wiola::app
