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

#include <vector>

namespace wiola::app {

class SearchIndex final {
public:
    /// Returns false for an unidentified record or one whose indexed fields did not change.
    bool upsert(const library::TrackRecord& track);
    bool remove(const library::MediaId& media);

    void clear() noexcept { entries_.clear(); }

    [[nodiscard]] std::vector<library::MediaId> query(const library::SearchQuery& query) const;

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    struct Entry {
        library::MediaId media;
        library::TrackRecord normalized;
    };

    std::vector<Entry> entries_;
};

} // namespace wiola::app
