/**
 * @file
 * @brief Incrementally maintained normalized track search fields.
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

#include <library/search.hpp>

#include <map>
#include <optional>
#include <vector>

namespace wiola::app {

class SearchIndex final {
public:
    bool upsert(const library::TrackRecord& track);
    bool remove(const library::MediaId& media);
    void clear();

    [[nodiscard]] bool contains(const library::MediaId& media) const
    {
        return entries_.contains(media);
    }

    [[nodiscard]] std::vector<library::MediaId> query(const library::SearchQuery& query) const;
    /// Caller normalizes the query. The borrowed result lasts until the next query or mutation.
    [[nodiscard]] const std::vector<library::MediaId>&
    query_normalized(const library::SearchQuery& query) const;

    /// Borrowed fields last until this entry is updated/removed, or the index is cleared.
    [[nodiscard]] const library::SearchFields* fields(const library::MediaId& id) const
    {
        const auto found = entries_.find(id);
        return found == entries_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    std::map<library::MediaId, library::SearchFields> entries_;
    mutable std::optional<library::SearchQuery> cached_query_;
    mutable std::vector<library::MediaId> cached_matches_;
    std::uint64_t revision_{};
};

} // namespace wiola::app
