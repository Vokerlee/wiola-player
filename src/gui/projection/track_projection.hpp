/**
 * @file
 * @brief One presentation pipeline for filtering and sorting track rows.
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
#include <condition_variable>
#include <desktop/queued_delivery.hpp>
#include <gui/model/tracks/track_view_state.hpp>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

namespace wiola::gui {

/// Contains no live repository, search-index or Qt-model references.
struct TrackProjectionInput {
    std::vector<TrackRow> rows;
    std::map<library::MediaId, library::SearchFields> search;
};

struct TrackProjectionResult {
    std::uint64_t generation;
    std::vector<TrackRow> rows;
};

/// One active and one replaceable pending query; completed payloads also coalesce before delivery.
class TrackProjection final {
public:
    using Observer = std::function<void(TrackProjectionResult)>;
    using Before = std::function<void(std::uint64_t, std::stop_token)>;
    TrackProjection(QObject& owner, Observer observer, Before before = {});
    ~TrackProjection();
    [[nodiscard]] std::uint64_t request(std::shared_ptr<const TrackProjectionInput> input,
        TrackViewState state);
    void cancel();
    void stop();

private:
    struct Request {
        std::uint64_t generation;
        std::shared_ptr<const TrackProjectionInput> input;
        TrackViewState state;
        std::shared_ptr<std::stop_source> cancellation;
    };

    struct Lifetime {
        TrackProjection* owner;
    };

    void run();
    void deliver();
    app::QueuedDelivery delivery_;
    std::shared_ptr<Lifetime> lifetime_;
    Observer observer_;
    Before before_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::optional<Request> pending_;
    std::optional<TrackProjectionResult> completed_;
    std::shared_ptr<std::stop_source> active_;
    std::uint64_t generation_{};
    bool stopping_{}, posted_{};
    std::jthread worker_;
};

} // namespace wiola::gui
