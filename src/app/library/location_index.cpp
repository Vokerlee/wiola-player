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
#include <app/library/local_identity.hpp>
#include <utility>

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
    std::set<library::MediaId> desired;
    for (const auto& path : record.locations)
        if (!path.empty())
            desired.insert(app::media_id_for(path));
    const auto previous = media_.find(record.id);
    if (previous != media_.end() && previous->second == desired)
        return;

    // Allocate new membership and reverse-ownership nodes before changing live bindings.
    decltype(media_) prepared_media;
    prepared_media.emplace(record.id, std::move(desired));
    const auto& wanted = prepared_media.begin()->second;
    decltype(locations_) additions;
    for (const auto& key : wanted)
        if (previous == media_.end() || !previous->second.contains(key))
            additions[key].insert(record.id);

    // Remove old membership and adopt prepared nodes without copying shared owner buckets.
    if (previous != media_.end())
        for (const auto& key : previous->second) {
            if (wanted.contains(key))
                continue;
            const auto found = locations_.find(key);
            found->second.erase(record.id);
            if (found->second.empty())
                locations_.erase(found);
        }
    for (auto& [key, members] : additions)
        if (const auto found = locations_.find(key); found != locations_.end())
            found->second.merge(members);
    locations_.merge(additions);
    media_.erase(record.id);
    media_.merge(prepared_media);
}

std::optional<library::MediaId> LocationIndex::find(const std::filesystem::path& path) const
{
    const auto found = locations_.find(app::media_id_for(path));
    return found == locations_.end() ? std::nullopt : std::optional{*found->second.begin()};
}

} // namespace wiola::app
