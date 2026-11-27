/**
 * @file
 * @brief Private playback orchestration and complete Session lifetime.
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

#include <loading/load_coordinator.hpp>
#include <loading/loader.hpp>
#include <metadata/metadata_coordinator.hpp>
#include <pipeline/output_session.hpp>
#include <pipeline/playback_pipeline.hpp>
#include <queue/playlist.hpp>
#include <wiola/playback/session.hpp>

#include <deque>
#include <memory>
#include <optional>

namespace wiola::engine {

/// All worker/storage ownership stays private to the playback implementation.
struct Session::Impl {
    Impl(core::Lease<audio::Stage> processing, OutputFactory make_output,
        std::shared_ptr<media::MediaProvider> provider, TransportConfig transport,
        BufferingConfig buffering);
    ~Impl() = default;
    OpenResult open(std::vector<media::ResourceReference> tracks);
    bool next_track();
    bool previous_track();
    void add(std::vector<media::ResourceReference> tracks);
    bool remove(std::size_t index);
    void clear();
    [[nodiscard]] const QueueView& playlist() const noexcept;
    bool play_track(std::size_t index);
    void set_repeat(RepeatMode repeat) noexcept;
    [[nodiscard]] RepeatMode repeat() const noexcept;
    void shuffle(bool on);
    [[nodiscard]] bool shuffled() const noexcept;
    void catch_up();
    [[nodiscard]] PlaybackSnapshot snapshot() const;
    [[nodiscard]] OperationId open_request_id() const noexcept;
    [[nodiscard]] OperationOutcomes take_outcomes();
    [[nodiscard]] bool reading() const noexcept;
    [[nodiscard]] OpenResult open_result() const noexcept;
    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] const media::ResourceReference& track() const noexcept;
    bool play() noexcept;
    bool pause() noexcept;
    bool resume() noexcept;
    void stop() noexcept;
    [[nodiscard]] std::optional<MediaReplacementState>
    release_media(const media::ResourceReference& resource);
    OpenResult restore_media(const MediaReplacementState& state);
    void seek(units::Time position) noexcept;
    [[nodiscard]] PlaybackState state() const noexcept;
    [[nodiscard]] bool playing() const noexcept;
    [[nodiscard]] units::Time time_played() const noexcept;
    [[nodiscard]] std::optional<units::Time> total_time() const noexcept;
    [[nodiscard]] bool seekable() const noexcept;
    [[nodiscard]] std::vector<std::string> playback_extensions() const;

private:
    /// Starts an identified load with the requested transport intent.
    OpenResult begin_reading(QueueEntryId entry, media::ResourceReference resource,
        LoadIntent intent, std::optional<units::Time> position = {});
    [[nodiscard]] bool wants_playback() const noexcept;
    void restore_when_ready();
    void record_outcome(OperationKind operation, OperationId id,
        std::optional<OperationError> error = std::nullopt);

    /// Puts on whatever has finished being read, and plays it if that is what it was read for.
    void take_up_finished_read();

    /// Puts what has been read about queued tracks into the queue.
    void take_up_finished_tags();

    /// Reads whatever the list now stands at, playing it if something was playing.
    OpenResult read_current_track(bool playing);

    /// Prepares a complete candidate before retiring current playback. Failure preserves the
    /// active pipeline; installation and its owned path are committed without allocation.
    bool install_track(std::unique_ptr<media::AudioDecoder> source,
        media::ResourceReference resource, QueueEntryId entry, OperationId request);

    /// Takes up the next track when one has run out. A listener who pressed stop, or a device
    /// that went away, is not a cue to play anything.
    void advance_if_ended();

    /// Retains the complete host-supplied processing graph across all prepared outputs.
    core::Lease<audio::Stage> processing_;
    std::shared_ptr<media::MediaProvider> provider_;
    Loader loader_;
    MetadataCoordinator metadata_;
    Playlist playlist_;

    LoadCoordinator loads_;
    OperationId open_request_id_{0};
    std::deque<OperationOutcome> outcomes_;
    bool outcomes_overflowed_{false};
    media::ResourceReference track_;
    QueueEntryId installed_entry_;

    struct Handoff {
        MediaReplacementState intent;
        bool restore_requested{false};
    };

    std::optional<Handoff> replacement_;
    std::uint64_t replacement_generation_{};
    OpenResult last_result_{OpenResult::opened};
    /// The cache retains reusable streams; each pipeline pins its own output independently.
    OutputSession out_;
    const TransportConfig transport_;
    const BufferingConfig buffering_;
    std::unique_ptr<PlaybackPipeline> pipeline_;
};

} // namespace wiola::engine
