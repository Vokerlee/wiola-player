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

#pragma once

#include <wiola/library/records.hpp>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace wiola::app {

/// Counts each normalized key once per media, independently of duplicate tag values.
class TagKeyCatalog final {
public:
    void upsert(const library::TrackRecord& track);
    void remove(const library::MediaId& id);
    [[nodiscard]] std::vector<std::string> keys() const;

    [[nodiscard]] std::uint64_t revision() const { return revision_; }

private:
    std::map<library::MediaId, std::set<std::string>> media_;

    struct Key {
        std::string label;
        std::size_t count{};
    };

    std::map<std::string, Key> keys_;
    std::uint64_t revision_{};
};

} // namespace wiola::app
