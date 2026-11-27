/**
 * @file
 * @brief The queue a run leaves behind, put back for the next one.
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

#include "queue_settings.hpp"

#include <wiola/media/local/resource.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace wiola::app {

QueueSettings::QueueSettings(app::PlaybackService& playback, QueueSettingsStore& settings,
    Available available)
    : playback_{playback}
    , settings_{settings}
    , available_{std::move(available)}
{
    if (!available_)
        throw std::invalid_argument{"queue restoration requires a track availability check"};
}

std::size_t QueueSettings::restore()
{
    const auto stored = settings_.read_queue();
    std::vector<std::filesystem::path> tracks;
    std::size_t gone{0};

    restored_.clear();

    std::vector<std::size_t> available;

    for (std::size_t i = 0; i < stored.tracks.size(); ++i) {
        if (available_(stored.tracks[i])) {
            tracks.push_back(stored.tracks[i]);
            available.push_back(i);
        } else
            ++gone;
    }

    playback_.set_repeat(stored.repeat);

    if (tracks.empty())
        return gone;

    const auto first_new = playback_.playlist().tracks().size();

    playback_.add(wiola::media::local_resources(tracks));

    if (playback_.playlist().tracks().size() != first_new + tracks.size())
        return gone + tracks.size();

    if (stored.occurrence_ids.size() == stored.tracks.size())
        for (std::size_t i = 0; i < available.size(); ++i)
            restored_.emplace(engine::QueueEntryId{stored.occurrence_ids[available[i]]},
                playback_.playlist().tracks()[first_new + i].id());

    if (stored.selected_occurrence) {
        const auto found = restored_.find(engine::QueueEntryId{*stored.selected_occurrence});
        if (found != restored_.end())
            if (const auto row = playback_.playlist().index_of(found->second))
                static_cast<void>(playback_.play_track(*row));
    } else if (!stored.has_occurrence_ids && stored.selected) {
        const auto selected{std::ranges::find(tracks, *stored.selected)};
        if (selected != tracks.end())
            static_cast<void>(playback_.play_track(first_new +
                static_cast<std::size_t>(std::distance(tracks.begin(), selected))));
    }

    if (stored.shuffled)
        playback_.shuffle(true);

    return gone;
}

void QueueSettings::save()
{
    const app::PlayerPresentation presentation{app::present_playback(playback_)};
    QueueSnapshot settings;
    settings.repeat = presentation.repeat;
    settings.shuffled = presentation.shuffled;

    for (const engine::QueueEntry& track : playback_.playlist().tracks()) {
        if (auto path = media::local_path(track.resource())) {
            settings.tracks.push_back(std::move(*path));
            settings.occurrence_ids.push_back(track.id().value);
        }
    }

    settings.has_occurrence_ids = true;

    if (const engine::QueueEntry* current = playback_.playlist().current()) {
        if (auto path = media::local_path(current->resource())) {
            settings.selected = std::move(*path);
            settings.selected_occurrence = current->id().value;
        }
    }

    settings_.write_queue(settings);
}

} // namespace wiola::app
