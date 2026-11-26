/**
 * @file
 * @brief Portable coalesced track projection with supplied owner-thread delivery.
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

#include "track_row.hpp"
#include "track_view_state.hpp"
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <stop_token>
#include <utility>
#include <vector>
#include <wiola/library/search.hpp>

namespace wiola::gui {

/// An immutable owned snapshot shared by queries; no live model/repository references.
class TrackProjectionInput final {
public:
    explicit TrackProjectionInput(std::vector<TrackRow> rows = {},
        std::map<library::MediaId, library::SearchFields> search = {})
        : rows_{std::move(rows)}
        , search_{std::move(search)}
    {
    }

    [[nodiscard]] const std::vector<TrackRow>& rows() const noexcept { return rows_; }

    [[nodiscard]] const std::map<library::MediaId, library::SearchFields>& search() const noexcept
    {
        return search_;
    }

private:
    const std::vector<TrackRow> rows_;
    const std::map<library::MediaId, library::SearchFields> search_;
};

struct TrackProjectionResult {
    std::uint64_t generation{};
    std::vector<TrackRow> rows;
    std::exception_ptr failure;
};

/// One active query, one replaceable pending query and one coalesced completion.
class TrackProjection final {
public:
    /// post queues work on the creating thread; discard closes/releases queued deliveries there.
    /// The supplied receiver remains alive until this workflow has stopped its worker.
    struct Delivery {
        std::function<bool(std::function<void()>)> post;
        std::function<void()> discard;
    };

    using Observer = std::function<void(TrackProjectionResult)>;
    using Before = std::function<void(std::uint64_t, std::stop_token)>;
    TrackProjection(Delivery delivery, Observer observer, Before before = {});
    ~TrackProjection();
    TrackProjection(const TrackProjection&) = delete;
    TrackProjection& operator=(const TrackProjection&) = delete;
    TrackProjection(TrackProjection&&) = delete;
    TrackProjection& operator=(TrackProjection&&) = delete;

    [[nodiscard]] std::uint64_t request(std::shared_ptr<const TrackProjectionInput> input,
        TrackViewState state);
    void cancel();
    void stop();

private:
    struct State;
    const std::shared_ptr<State> state_;
};

} // namespace wiola::gui
