/**
 * @file
 * @brief Small application facade over playback, queue, loading, and audio values.
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

#pragma once

#include <engine/session.hpp>

namespace wiola::engine {

enum class CommandResult {
    accepted,
    unavailable,
};

class PlaybackService {
public:
    PlaybackService();
    explicit PlaybackService(OutputFactory make_output);
    PlaybackService(OutputFactory make_output, std::shared_ptr<media::MediaProvider> provider);
    PlaybackService(OutputFactory make_output, DecoderOpener open_decoder,
        MetadataReader read_metadata);

    OpenResult open(std::vector<std::filesystem::path> tracks);
    void add(std::vector<std::filesystem::path> tracks);
    bool remove(std::size_t index);
    void clear();
    bool play_track(std::size_t index);
    bool next_track();
    bool previous_track();

    CommandResult play() noexcept;
    CommandResult pause() noexcept;
    CommandResult resume() noexcept;
    void stop() noexcept;
    void seek(units::Time position) noexcept;

    void set_volume(float gain) noexcept;
    [[nodiscard]] float volume() const noexcept;
    void set_equalizer_enabled(bool enabled) noexcept;
    [[nodiscard]] bool equalizer_enabled() const noexcept;
    void set_equalizer_band(std::size_t index, float db) noexcept;
    [[nodiscard]] float equalizer_band(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t num_equalizer_bands() const noexcept;

    /// Drains completions and advances playback; schedulers and headless callers share this pump.
    PlaybackSnapshot pump();
    [[nodiscard]] PlaybackSnapshot snapshot() const noexcept;
    void catch_up();
    [[nodiscard]] bool reading() const noexcept;
    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] bool playing() const noexcept;
    [[nodiscard]] OpenResult open_result() const noexcept;
    [[nodiscard]] Playback::State state() const noexcept;
    [[nodiscard]] units::Time time_played() const noexcept;
    [[nodiscard]] units::Time total_time() const noexcept;
    [[nodiscard]] const Playlist& playlist() const noexcept;
    [[nodiscard]] const std::filesystem::path& track() const noexcept;

    void set_repeat(Playlist::Repeat repeat) noexcept;
    [[nodiscard]] Playlist::Repeat repeat() const noexcept;
    void shuffle(bool on);
    [[nodiscard]] bool shuffled() const noexcept;

private:
    Session session_;
};

} // namespace wiola::engine
