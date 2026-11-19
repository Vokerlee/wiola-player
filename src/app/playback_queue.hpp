/**
 * @file
 * @brief Explicit saved-document commands for the real playback queue.
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

#include <engine/session.hpp>
#include <library/repository.hpp>

namespace wiola::app {

struct PlaybackQueueResult {
    engine::OpenResult open_result{engine::OpenResult::unsupported};
    std::size_t queued{0};
    std::size_t unavailable{0};
    bool requested{false};
};

class PlaybackQueue final {
public:
    [[nodiscard]] PlaybackQueueResult replace_with_saved_playlist(engine::Session& session,
        const library::LibraryRepository& repository, const library::PlaylistId& id) const;
};

} // namespace wiola::app
