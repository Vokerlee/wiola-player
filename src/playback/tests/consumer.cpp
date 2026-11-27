/**
 * @file
 * @brief Independent target consumer of portable playback values.
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
#include <wiola/playback/queue_view.hpp>
#include <wiola/playback/repeat_mode.hpp>
#include <wiola/playback/snapshot.hpp>

#include <algorithm>
#include <type_traits>
#include <vector>

static_assert(
    std::is_same_v<decltype(wiola::engine::PlaybackSnapshot::state), wiola::engine::PlaybackState>
);

namespace {

// A downstream queue needs public values only; it does not import Wiola's storage policy.
class ConsumerQueue final : public wiola::engine::QueueView {
public:
    std::vector<wiola::engine::QueueEntry> entries;
    std::optional<std::size_t> selected;
    std::size_t structure{}, metadata{};

    std::span<const wiola::engine::QueueEntry> tracks() const noexcept override { return entries; }

    const wiola::engine::QueueEntry* current() const noexcept override
    {
        return selected ? &entries[*selected] : nullptr;
    }

    std::optional<std::size_t> position() const noexcept override { return selected; }

    std::optional<std::size_t> index_of(wiola::engine::QueueEntryId id) const noexcept override
    {
        const auto found{std::ranges::find(entries, id, &wiola::engine::QueueEntry::id)};
        return found == entries.end() ? std::nullopt
                                      : std::optional<std::size_t>{found - entries.begin()};
    }

    std::size_t structural_revision() const noexcept override { return structure; }

    std::size_t metadata_revision() const noexcept override { return metadata; }
};

} // namespace

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
    snapshot.track = media::ResourceReference{"consumer", "owned resource", "Owned resource"};
    snapshot.queue_position = 0;

    const auto retained = snapshot;

    snapshot.track = {};

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

    const engine::QueueEntry entry{
        engine::QueueEntryId{receipt.id},
        media::ResourceReference{"consumer", "resource", "Resource"},
        facts
    };

    facts.title.clear();

    if (entry.id() != engine::QueueEntryId{receipt.id} || entry.resource().key() != "resource" ||
        entry.metadata().title != "owned title" || entry.artwork() || entry.revision() != 0)
        return 5;

    ConsumerQueue queue;
    const engine::QueueView& view{queue};

    if (!view.empty() || view.current() || view.position() || view.index_of(entry.id()))
        return 6;

    queue.entries.push_back(entry);
    queue.selected = 0;
    ++queue.structure;

    const auto owned_entry{view.tracks().front()};

    queue.entries.emplace_back(engine::QueueEntryId{receipt.id + 1},
        media::ResourceReference{"consumer", "resource", "Resource"});
    ++queue.structure;

    if (view.tracks().size() != 2 || view.current()->id() != entry.id() || view.position() != 0 ||
        view.index_of(engine::QueueEntryId{receipt.id + 1}) != 1 ||
        view.structural_revision() != 2 || view.metadata_revision() != 0 ||
        owned_entry.metadata().title != "owned title")
        return 7;

    return !engine::QueueEntryId{receipt.id}.valid();
}
