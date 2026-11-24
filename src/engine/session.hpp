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

#include <core/macros.hpp>
#include <engine/buffering_config.hpp>
#include <engine/load/load_coordinator.hpp>
#include <engine/load/loader.hpp>
#include <engine/load/metadata_coordinator.hpp>
#include <engine/pipeline/output_session.hpp>
#include <engine/pipeline/playback_pipeline.hpp>
#include <engine/queue/playlist.hpp>
#include <transport/transport_config.hpp>
#include <wiola/audio/equalizer.hpp>
#include <wiola/audio/processing_chain.hpp>
#include <wiola/audio/volume.hpp>
#include <wiola/media/audio_decoder.hpp>
#include <wiola/playback/open_result.hpp>
#include <wiola/playback/operation_outcome.hpp>
#include <wiola/playback/snapshot.hpp>
#include <wiola/units.hpp>

#include <deque>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace wiola::engine {

/**
 * Builds what a track is played through, and keeps it built while it plays.
 *
 * A buffer and an output are cut for the format of the track that is loaded, so opening another
 * one replaces them. The chain outlives them all: a setting belongs to the listener rather than
 * to what is playing.
 */
class Session {
public:
    /// The host supplies output creation and one format-neutral provider for loading/metadata.
    Session(OutputFactory make_output, std::shared_ptr<media::MediaProvider> provider);

    /// Uses the supplied media collaborators, for deterministic engine tests.
    Session(OutputFactory make_output, DecoderOpener open_decoder, MetadataReader read_metadata);

    /// Uses independently owned transport and buffering policies.
    Session(OutputFactory make_output, DecoderOpener open_decoder, MetadataReader read_metadata,
        TransportConfig transport, BufferingConfig buffering);

    NO_COPY_SEMANTIC(Session);
    NO_MOVE_SEMANTIC(Session);

    ~Session();

    /// Plays `tracks` in the order given, standing at the first of them and reading it. What
    /// was in the list before is replaced. Reading a file is not quick enough to wait for, so
    /// this returns `loading` and whatever is playing keeps playing; `catch_up` is what finishes
    /// the job. A file that will not open leaves the track that was playing alone, and says why
    /// through `open_result`.
    OpenResult open(std::vector<std::filesystem::path> tracks);

    /// Stands at the next track of the list and reads it, keeping whether something was playing.
    /// False when there is nowhere to go.
    bool next_track();

    /// The same, backwards.
    bool previous_track();

    /// Puts `tracks` at the end of the queue, leaving what is playing alone. With nothing
    /// loaded, the first of them is read so that there is something to press play on.
    void add(std::vector<std::filesystem::path> tracks);

    /// Takes the track that was put at `index` out of the queue. What is playing keeps playing,
    /// even when it is the one taken out: the queue says what comes next, not what is on.
    bool remove(std::size_t index);

    /// Empties the queue and ends playback. What was playing stays loaded, so it can be played
    /// again without opening it afresh.
    void clear();

    /// What is queued, and where in it playback stands. For showing a listener their list.
    [[nodiscard]] const Playlist& playlist() const noexcept;

    /// Plays the track that was put at `index`, keeping whether something was playing. False
    /// when there is no such track.
    bool play_track(std::size_t index);

    void set_repeat(Playlist::Repeat repeat) noexcept;
    [[nodiscard]] Playlist::Repeat repeat() const noexcept;

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

    /// The file that is loaded, or nothing when none is. A load that fails leaves it as it was,
    /// along with the track it names.
    [[nodiscard]] const std::filesystem::path& track() const noexcept;

    /// Starts, pauses or resumes, whichever playback is due. False when the output could not
    /// be opened, and when there is nothing loaded.
    bool play_or_pause();
    bool play() noexcept;
    bool pause() noexcept;
    bool resume() noexcept;

    /// Ends playback and returns to the beginning of the track.
    void stop() noexcept;

    /// Quiesces the current decoder and reads it again, restoring play intent and position when
    /// the replacement is ready. Returns immediately while that read continues.
    struct MediaReplacementState {
        QueueEntryId entry;
        bool play;
        units::Time position;
        std::uint64_t generation{};
    };

    [[nodiscard]] std::optional<MediaReplacementState>
    release_media(const std::filesystem::path& path);
    OpenResult restore_media(const MediaReplacementState& state);
    OpenResult reload_current();

    /// Moves playback to `position`, measured from the start of the track.
    void seek(units::Time position) noexcept;

    [[nodiscard]] PlaybackState state() const noexcept;
    [[nodiscard]] bool playing() const noexcept;
    [[nodiscard]] units::Time time_played() const noexcept;
    [[nodiscard]] std::optional<units::Time> total_time() const noexcept;
    [[nodiscard]] bool seekable() const noexcept;
    [[nodiscard]] std::vector<std::string> playback_extensions() const;

    [[nodiscard]] audio::Volume& volume() noexcept { return volume_; }

    [[nodiscard]] const audio::Volume& volume() const noexcept { return volume_; }

    [[nodiscard]] audio::Equalizer& equalizer() noexcept { return equalizer_; }

    [[nodiscard]] const audio::Equalizer& equalizer() const noexcept { return equalizer_; }

private:
    /// Starts an identified load with the requested transport intent.
    OpenResult begin_reading(const Track& track, LoadIntent intent);
    void record_outcome(OperationKind operation, OperationId id, media::Result<void> result);

    /// Puts on whatever has finished being read, and plays it if that is what it was read for.
    void take_up_finished_read();

    /// Puts what has been read about queued tracks into the queue.
    void take_up_finished_tags();

    /// Reads whatever the list now stands at, playing it if something was playing.
    OpenResult read_current_track(bool playing);

    /// Whether a track has been read and is waiting to go on.
    [[nodiscard]] bool read_waiting() const noexcept;

    /// Puts the track that was read in place of whatever is loaded, from its beginning and not
    /// playing. False when nothing is waiting.
    bool install_read_track();

    /// Takes up the next track when one has run out. A listener who pressed stop, or a device
    /// that went away, is not a cue to play anything.
    void advance_if_ended();

    /// What a track is shaped by. Declared before the chain that runs them, so the chain is let
    /// go while the steps it points at are still there. Which order they run in is settled where
    /// they are put into the chain, not here.
    audio::Equalizer equalizer_{pcm::StreamSpec{}};
    audio::Volume volume_;
    audio::ProcessingChain chain_;
    std::shared_ptr<media::MediaProvider> provider_;
    std::unique_ptr<Loader> loader_;
    MetadataCoordinator metadata_;
    Playlist playlist_;

    LoadCoordinator loads_;
    OperationId open_request_id_{0};
    std::deque<OperationOutcome> outcomes_;
    bool outcomes_overflowed_{false};
    std::filesystem::path track_;

    /// A track that has been read and not yet put on, with the name it goes by.
    std::unique_ptr<media::AudioDecoder> ready_;
    std::filesystem::path ready_track_;
    std::optional<units::Time> restore_position_;
    std::optional<MediaReplacementState> replacement_;
    std::uint64_t replacement_generation_{};
    OpenResult last_result_{OpenResult::opened};
    /// The output outlives its pipeline so transport is joined before the output is disconnected.
    OutputSession out_;
    TransportConfig transport_;
    BufferingConfig buffering_;
    std::unique_ptr<PlaybackPipeline> pipeline_;
};

} // namespace wiola::engine
