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

#pragma once
#include <library/records.hpp>
#include <map>
#include <optional>
#include <set>

namespace wiola::app {

/// Cached normalized-location lookup, maintained by committed media batches.
class LocationIndex final {
public:
    void upsert(const library::TrackRecord& record);
    void remove(const library::MediaId& id);
    [[nodiscard]] std::optional<library::MediaId> find(const std::filesystem::path& path) const;

private:
    std::map<library::MediaId, std::set<library::MediaId>> media_;
    std::map<library::MediaId, std::set<library::MediaId>> locations_;
};

} // namespace wiola::app
