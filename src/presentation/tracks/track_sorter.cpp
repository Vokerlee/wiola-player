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

struct PreparedRow {
    std::size_t source;
    std::vector<TrackSortValue> values;
    TrackSortValue path, title;
};

int compare(const PreparedRow& first, const PreparedRow& second, const std::vector<TrackRow>& rows,
    const std::vector<TrackSortKey>& keys)
{
    for (std::size_t i = 0; i < keys.size(); ++i) {
        int result = compare_values(first.values[i], second.values[i]);
        if (keys[i].direction == TrackSortDirection::descending &&
            !std::holds_alternative<std::monostate>(first.values[i]) &&
            !std::holds_alternative<std::monostate>(second.values[i]))
            result = -result;
        if (result)
            return result;
    }
    if (const auto result = compare_values(first.path, second.path))
        return result;
    if (const auto result = compare_values(first.title, second.title))
        return result;
    return rows[first.source].id < rows[second.source].id ? -1
        : rows[second.source].id < rows[first.source].id  ? 1
                                                          : 0;
}

} // namespace

std::vector<TrackRow> sorted_rows(const std::vector<TrackRow>& tracks,
    const library::LibraryRepository* repository, const std::vector<TrackSortKey>& keys,
    TrackSortStatistics* statistics, std::stop_token stop)
{
    if (statistics)
        *statistics = {};
    std::vector<TrackSortKey> valid;
    for (const auto& key : keys)
        if (key.valid())
            valid.push_back(key);
    const TrackSortKey path{.field = TrackSortField::path}, title{.field = TrackSortField::title};
    std::vector<PreparedRow> prepared;
    prepared.reserve(tracks.size());
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if ((i % 64) == 0 && stop.stop_requested())
            return {};
        PreparedRow row{.source = i,
            .values = {},
            .path = track_sort_value(path, tracks[i], repository),
            .title = track_sort_value(title, tracks[i], repository)};
        row.values.reserve(valid.size());
        for (const auto& key : valid)
            row.values.push_back(track_sort_value(key, tracks[i], repository));
        if (statistics)
            statistics->prepared_values += valid.size() + 2;
        prepared.push_back(std::move(row));
    }

    struct Cancelled { };

    std::size_t compared = 0;
    try {
        std::stable_sort(prepared.begin(), prepared.end(),
            [&](const auto& first, const auto& second) {
                if ((++compared % 1024) == 0 && stop.stop_requested())
                    throw Cancelled{};
                if (statistics)
                    ++statistics->comparisons;
                return compare(first, second, tracks, valid) < 0;
            });
    } catch (const Cancelled&) {
        return {};
    }
    if (stop.stop_requested())
        return {};
    std::vector<TrackRow> sorted;
    sorted.reserve(tracks.size());
    for (const auto& row : prepared)
        sorted.push_back(tracks[row.source]);
    return sorted;
}

} // namespace wiola::gui
