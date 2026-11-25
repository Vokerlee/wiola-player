/**
 * @file
 * @brief Incremental catalog of normalized custom-tag keys.
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
#include <utility>

namespace wiola::app {

void TagKeyCatalog::remove(const library::MediaId& id)
{
    const auto found = media_.find(id);
    if (found == media_.end())
        return;
    for (const auto& key : found->second) {
        const auto item = keys_.find(key);
        if (--item->second.count == 0) {
            keys_.erase(item);
            ++revision_;
        }
    }
    media_.erase(found);
}

void TagKeyCatalog::upsert(const library::TrackRecord& track)
{
    if (!track.id.valid())
        return;
    std::map<std::string, std::string> incoming;
    for (const auto& tag : track.tags)
        if (auto key = library::make_tag_key(tag.key))
            incoming.try_emplace(library::normalized_tag_text(*key), *key);
    std::set<std::string> desired;
    for (const auto& [key, label] : incoming)
        desired.insert(key);
    const auto previous = media_.find(track.id);
    if (previous != media_.end() && previous->second == desired)
        return;

    decltype(media_) prepared_media;
    prepared_media.emplace(track.id, std::move(desired));
    const auto& wanted = prepared_media.begin()->second;
    decltype(keys_) prepared_keys;
    std::uint64_t changes = 0;
    const auto prepare = [&](const std::string& key) -> Key& {
        auto& result = prepared_keys[key];
        if (const auto found = keys_.find(key); found != keys_.end())
            result = found->second;
        return result;
    };
    if (previous != media_.end())
        for (const auto& key : previous->second) {
            if (wanted.contains(key))
                continue;
            if (--prepare(key).count == 0)
                ++changes;
        }
    for (const auto& key : wanted) {
        if (previous != media_.end() && previous->second.contains(key))
            continue;
        auto& item = prepare(key);
        if (item.count++ == 0) {
            item.label = incoming.at(key);
            ++changes;
        }
    }

    // All owned nodes and labels exist before committing counters and reverse ownership.
    for (const auto& [key, item] : prepared_keys)
        keys_.erase(key);
    std::erase_if(prepared_keys, [](const auto& item) { return item.second.count == 0; });
    keys_.merge(prepared_keys);
    media_.erase(track.id);
    media_.merge(prepared_media);
    revision_ += changes;
}

std::vector<std::string> TagKeyCatalog::keys() const
{
    std::vector<std::string> result;
    for (const auto& [key, item] : keys_)
        result.push_back(item.label);
    return result;
}

} // namespace wiola::app
