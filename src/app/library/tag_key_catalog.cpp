/**
 * @file
 * @brief One normalized search, filter, and sort state for each track browser.
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

#include "tag_key_catalog.hpp"
#include <library/tags.hpp>

namespace wiola::app {

void TagKeyCatalog::remove(const library::MediaId& id)
{
    const auto found = media_.find(id);
    if (found == media_.end())
        return;
    for (const auto& key : found->second) {
        const auto item = keys_.find(key);
        if (--item->second.count == 0)
            keys_.erase(item);
    }
    media_.erase(found);
}

void TagKeyCatalog::upsert(const library::TrackRecord& track)
{
    if (!track.id.valid())
        return;
    std::map<std::string, std::string> incoming;
    for (const auto& tag : track.tags)
        if (auto valid = library::make_tag_value(tag.key, "value"))
            incoming.try_emplace(library::normalized_tag_text(valid->key), valid->key);
    std::set<std::string> desired;
    for (const auto& [key, label] : incoming)
        desired.insert(key);
    auto& previous = media_[track.id];
    for (const auto& key : previous) {
        if (desired.contains(key))
            continue;
        const auto item = keys_.find(key);
        if (--item->second.count == 0)
            keys_.erase(item);
    }
    for (const auto& [key, label] : incoming) {
        if (previous.contains(key))
            continue;
        auto& item = keys_[key];
        if (item.count++ == 0)
            item.label = label;
    }
    previous = std::move(desired);
}

std::vector<std::string> TagKeyCatalog::keys() const
{
    std::vector<std::string> result;
    for (const auto& [key, item] : keys_)
        result.push_back(item.label);
    return result;
}

} // namespace wiola::app
