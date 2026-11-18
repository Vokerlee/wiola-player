/**
 * @file
 * @brief Persistent commands and presentation state for player widgets.
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

#include <app/playback_service.hpp>

namespace wiola::gui {

/// Everything a player view needs after one application pump, including queue playback policy.
struct PlayerPresentation {
    engine::PlaybackSnapshot playback;
    engine::Playlist::Repeat repeat{engine::Playlist::Repeat::none};
    bool shuffled{false};
};

/**
 * Owns the application playback facade and presents its commands without exposing `Session`.
 *
 * A presenter can outlive any widget tree that observes it. Widgets receive typed values and ask
 * for commands here rather than coordinating transport, queue policy, and asynchronous loading
 * themselves.
 */
class PlayerPresenter {
public:
    PlayerPresenter();
    explicit PlayerPresenter(engine::OutputFactory make_output);
    PlayerPresenter(engine::OutputFactory make_output, engine::DecoderOpener open_decoder,
        engine::MetadataReader read_metadata);

    engine::OperationReceipt open(std::vector<std::filesystem::path> tracks);
    void add(std::vector<std::filesystem::path> tracks);
    bool remove(std::size_t index);
    void clear();
    bool play_track(std::size_t index);
    bool next_track();
    bool previous_track();

    engine::CommandResult toggle_playback() noexcept;
    void stop() noexcept;
    void seek_to(double fraction) noexcept;

    void set_volume(float gain) noexcept;
    [[nodiscard]] float volume() const noexcept;
    void set_equalizer_enabled(bool enabled) noexcept;
    [[nodiscard]] bool equalizer_enabled() const noexcept;
    void set_equalizer_band(std::size_t index, float db) noexcept;
    [[nodiscard]] float equalizer_band(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t num_equalizer_bands() const noexcept;

    void cycle_repeat() noexcept;
    void shuffle(bool on);
    [[nodiscard]] const engine::Playlist& playlist() const noexcept;
    [[nodiscard]] std::vector<std::string> playback_extensions() const;

    PlayerPresentation pump();
    [[nodiscard]] PlayerPresentation presentation() const;

private:
    engine::PlaybackService service_;
};

} // namespace wiola::gui
