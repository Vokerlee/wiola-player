/**
 * @file
 * @brief One track, everything it plays through, and what the listener asked of it.
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

#include <wiola/foundation/lease.hpp>
#include <wiola/media/resource.hpp>
#include <wiola/playback/buffering_config.hpp>
#include <wiola/playback/open_result.hpp>
#include <wiola/playback/operation_outcome.hpp>
#include <wiola/playback/output_factory.hpp>
#include <wiola/playback/queue_view.hpp>
#include <wiola/playback/repeat_mode.hpp>
#include <wiola/playback/snapshot.hpp>
#include <wiola/playback/transport_config.hpp>
#include <wiola/units.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace wiola::media {

class MediaProvider;

} // namespace wiola::media

namespace wiola::audio {

class Stage;

} // namespace wiola::audio

namespace wiola::engine {

/**
 * Builds what a track is played through, and keeps it built while it plays.
 *
 * One control thread calls commands and observations. Decoder work and device callbacks run
 * on owned workers; completion delivery and installation are observed after a control turn.
 *
 * A buffer and an output are cut for the format of the track that is loaded, so opening another
 * one replaces them. The supplied lease retains the processing stage across their lifetimes.
 * Built-in control/settings composition belongs to the application rather than to the playback
 * kernel. The stage serves this Session's processing role; sharing it requires an implementation
 * that explicitly supports that concurrency, and configuration requires all of its prior callbacks
 * to be quiescent.
 */
class Session {
public:
    /// The host supplies output creation and one format-neutral provider for loading/metadata.
    /// Null providers or invalid per-instance policies fail synchronously before work can start.
    Session(core::Lease<audio::Stage> processing, OutputFactory make_output,
        std::shared_ptr<media::MediaProvider> provider, TransportConfig transport = {},
        BufferingConfig buffering = {});

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    ~Session();

    /// Plays `tracks` in the order given, standing at the first of them and reading it. What
    /// was in the list before is replaced. Reading a file is not quick enough to wait for, so
    /// this returns `loading` and whatever is playing keeps playing; `catch_up` is what finishes
    /// the job. A file that will not open leaves the track that was playing alone, and says why
    /// through `open_result`.
    OpenResult open(std::vector<media::ResourceReference> tracks);

    /// Stands at the next track of the list and reads it, keeping whether something was playing.
    /// False when there is nowhere to go.
    bool next_track();

    /// The same, backwards.
    bool previous_track();

    /// Puts `tracks` at the end of the queue, leaving what is playing alone. With nothing
    /// loaded, the first of them is read so that there is something to press play on.
    void add(std::vector<media::ResourceReference> tracks);

    /// Takes the track that was put at `index` out of the queue. What is playing keeps playing,
    /// even when it is the one taken out: the queue says what comes next, not what is on.
    bool remove(std::size_t index);

    /// Empties the queue and ends playback. What was playing stays loaded, so it can be played
    /// again without opening it afresh.
    void clear();

    /// What is queued, and where in it playback stands. For showing a listener their list.
    [[nodiscard]] const QueueView& playlist() const noexcept;

    /// Plays the track that was put at `index`, keeping whether something was playing. False
    /// when there is no such track.
    bool play_track(std::size_t index);

    void set_repeat(RepeatMode repeat) noexcept;
    [[nodiscard]] RepeatMode repeat() const noexcept;

    /// Plays the list in an order drawn at random, or in the order it was given.
    void shuffle(bool on);
    [[nodiscard]] bool shuffled() const noexcept;

    /// Does whatever has fallen due since the last time: puts on a track that has finished
    /// being read, and takes up the next one when the last has run out. For the thread that
    /// opens tracks, as often as it likes - a caller that draws should call it as it draws.
    void catch_up();
    [[nodiscard]] PlaybackSnapshot snapshot() const;
    [[nodiscard]] OperationId open_request_id() const noexcept;
    [[nodiscard]] OperationOutcomes take_outcomes();

    /// Whether a file is still being read.
    [[nodiscard]] bool reading() const noexcept;

    /// How the last open went, which is `loading` until the file has been read.
    [[nodiscard]] OpenResult open_result() const noexcept;

    [[nodiscard]] bool loaded() const noexcept;

    /// The last installed resource, empty before the first installation. A failed candidate
    /// or temporary handoff retains its identity independently of the selected queue entry.
    [[nodiscard]] const media::ResourceReference& track() const noexcept;

    bool play() noexcept;
    bool pause() noexcept;
    bool resume() noexcept;

    /// Ends playback and returns to the beginning of the track.
    void stop() noexcept;

    /// Identifies an occurrence/generation to restore after a temporary installed-media handoff.
    /// The held intent remains owned by Session; later commands supersede this copied snapshot.
    struct MediaReplacementState {
        QueueEntryId entry;
        bool play;
        units::Time position;
        std::uint64_t generation{};
    };

    /// Quiesces the installed decoder/output when path names its resource, independently of
    /// queue selection. A pending open for the same resource is cancelled with its intent kept;
    /// an unrelated open remains pending. This does not exclude other provider/metadata I/O.
    [[nodiscard]] std::optional<MediaReplacementState>
    release_media(const media::ResourceReference& resource);
    /// Restores the latest held intent without changing queue selection. An unrelated pending
    /// open takes precedence: success revokes the ticket, failure permits deferred restoration.
    /// New selections, queue replacement/removal of the held occurrence, or clear revoke it.
    OpenResult restore_media(const MediaReplacementState& state);

    /// Moves playback to `position`, measured from the start of the track.
    void seek(units::Time position) noexcept;

    [[nodiscard]] PlaybackState state() const noexcept;
    [[nodiscard]] bool playing() const noexcept;
    [[nodiscard]] units::Time time_played() const noexcept;
    [[nodiscard]] std::optional<units::Time> total_time() const noexcept;
    [[nodiscard]] bool seekable() const noexcept;
    [[nodiscard]] std::vector<std::string> playback_extensions() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wiola::engine
