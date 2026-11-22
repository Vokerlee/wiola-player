/**
 * @file
 * @brief Stable multi-column track ordering with deterministic fallbacks.
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

#include "track_sorter.hpp"

#include <algorithm>

namespace wiola::gui {

namespace {

int compare_values(const TrackSortValue& first, const TrackSortValue& second)
{
    const bool first_missing = std::holds_alternative<std::monostate>(first);
    const bool second_missing = std::holds_alternative<std::monostate>(second);

    if (first_missing || second_missing) {
        if (first_missing != second_missing)
            return first_missing ? 1 : -1;

        return 0;
    }

    if (const auto* text = std::get_if<std::string>(&first)) {
        const std::string& other = std::get<std::string>(second);
        return *text < other ? -1 : *text > other ? 1 : 0;
    }

    const units::Time duration = std::get<units::Time>(first);
    const units::Time other = std::get<units::Time>(second);
    return duration < other ? -1 : other < duration ? 1 : 0;
}

int compare(const TrackRow& first, const TrackRow& second,
    const library::LibraryRepository* repository, const std::vector<TrackSortKey>& keys)
{
    for (const TrackSortKey& key : keys) {
        const TrackSortValue first_value = track_sort_value(key, first, repository);
        const TrackSortValue second_value = track_sort_value(key, second, repository);
        int result = compare_values(first_value, second_value);

        if (key.direction == TrackSortDirection::descending &&
            !std::holds_alternative<std::monostate>(first_value) &&
            !std::holds_alternative<std::monostate>(second_value))
            result = -result;

        if (result != 0)
            return result;
    }

    const TrackSortKey path{.field = TrackSortField::path,
        .direction = TrackSortDirection::ascending,
        .tag_key = {}};
    if (const int result = compare_values(track_sort_value(path, first, repository),
            track_sort_value(path, second, repository)))
        return result;

    const TrackSortKey title{.field = TrackSortField::title,
        .direction = TrackSortDirection::ascending,
        .tag_key = {}};
    if (const int result = compare_values(track_sort_value(title, first, repository),
            track_sort_value(title, second, repository)))
        return result;

    return first.id < second.id ? -1 : second.id < first.id ? 1 : 0;
}

} // namespace

std::vector<TrackRow> sorted_rows(const std::vector<TrackRow>& tracks,
    const library::LibraryRepository* repository, const std::vector<TrackSortKey>& keys)
{
    std::vector<TrackSortKey> valid;

    for (const TrackSortKey& key : keys) {
        if (key.valid())
            valid.push_back(key);
    }

    std::vector<TrackRow> sorted = tracks;
    std::stable_sort(sorted.begin(), sorted.end(),
        [&](const TrackRow& first, const TrackRow& second) {
            return compare(first, second, repository, valid) < 0;
        });

    return sorted;
}

} // namespace wiola::gui
