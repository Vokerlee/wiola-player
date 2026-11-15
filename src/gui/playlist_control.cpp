/**
 * @file
 * @brief The queue a run leaves behind, put back for the next one.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
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

#include "playlist_control.hpp"

#include "player_settings.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace wiola::gui {

namespace {

engine::Playlist::Repeat repeat_of(int stored) noexcept
{
    using Repeat = engine::Playlist::Repeat;

    switch (stored) {
    case static_cast<int>(Repeat::track):
        return Repeat::track;
    case static_cast<int>(Repeat::all):
        return Repeat::all;
    default:
        return Repeat::none;
    }
}

} // namespace

PlaylistControl::PlaylistControl(engine::Session& session, QSettings& settings) noexcept
    : session_{session}
    , settings_{settings}
{
}

std::size_t PlaylistControl::restore()
{
    PlaylistSettings stored{QtSettingsAdapter{settings_}.read_playlist()};
    std::vector<std::filesystem::path> tracks;
    std::size_t gone{0};

    for (const std::filesystem::path& track : stored.tracks) {
        if (std::filesystem::exists(track))
            tracks.push_back(track);
        else
            ++gone;
    }

    session_.set_repeat(repeat_of(stored.repeat));

    if (tracks.empty())
        return gone;

    session_.add(tracks);

    if (stored.selected) {
        const auto selected{std::ranges::find(tracks, *stored.selected)};
        if (selected != tracks.end())
            static_cast<void>(session_.play_track(
                static_cast<std::size_t>(std::distance(tracks.begin(), selected))
            ));
    }

    if (stored.shuffled)
        session_.shuffle(true);

    return gone;
}

void PlaylistControl::save()
{
    PlaylistSettings settings{.tracks = {},
        .selected = {},
        .repeat = static_cast<int>(session_.repeat()),
        .shuffled = session_.shuffled()};

    for (const engine::Track& track : session_.playlist().tracks())
        settings.tracks.push_back(track.path());

    if (const engine::Track* current = session_.playlist().current())
        settings.selected = current->path();

    QtSettingsAdapter{settings_}.write_playlist(settings);
}

} // namespace wiola::gui
