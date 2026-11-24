/**
 * @file
 * @brief Independent installed consumer of portable playback values.
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

#include <wiola/playback/operation_outcome.hpp>
#include <wiola/playback/queue_entry.hpp>
#include <wiola/playback/queue_entry_id.hpp>
#include <wiola/playback/snapshot.hpp>

#include <type_traits>

static_assert(
    std::is_same_v<decltype(wiola::engine::PlaybackSnapshot::state), wiola::engine::PlaybackState>
);

int main()
{
    using namespace wiola;
    engine::OperationReceipt receipt{.id = 7,
        .operation = engine::OperationKind::open,
        .acceptance = engine::CommandResult::accepted};
    engine::OperationOutcomes completed;
    completed.values.push_back({
        .id = receipt.id,
        .operation = receipt.operation,
        .error = media::Error{media::ErrorCategory::damaged, "controlled failure"}
    });
    completed.overflowed = true;
    if (!completed.values.front().error || completed.values.front().id != receipt.id ||
        !completed.overflowed)
        return 1;
    engine::PlaybackSnapshot snapshot;
    snapshot.state = engine::PlaybackState::faulted;
    snapshot.open_result = engine::open_failed(*completed.values.front().error);
    snapshot.track = "owned resource";
    snapshot.queue_position = 0;
    const auto retained = snapshot;
    snapshot.track.clear();
    if (retained.track.empty() || retained.total_time ||
        retained.open_result != engine::OpenResult::damaged)
        return 2;
    engine::OperationOutcome device_failure{.id = receipt.id,
        .operation = receipt.operation,
        .error = engine::OutputSetupError{"controlled device failure"}};
    if (!device_failure.error ||
        engine::open_failed(*device_failure.error) != engine::OpenResult::output_unavailable)
        return 3;
    engine::OperationOutcome success;
    if (success.error)
        return 4;
    media::TrackMetadata facts;
    facts.title = "owned title";
    const engine::QueueEntry entry{engine::QueueEntryId{receipt.id}, "resource", facts};
    facts.title.clear();
    if (entry.id() != engine::QueueEntryId{receipt.id} || entry.path() != "resource" ||
        entry.metadata().title != "owned title" || entry.artwork() || entry.revision() != 0)
        return 5;
    return !engine::QueueEntryId{receipt.id}.valid();
}
