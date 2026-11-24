/**
 * @file
 * @brief Small application facade over playback, queue, loading, and audio values.
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

#include "playback_presentation.hpp"
#include <engine/session.hpp>
#include <wiola/audio/equalizer_config.hpp>
#include <wiola/audio/volume_policy.hpp>
#include <wiola/playback/operation_outcome.hpp>
#include <wiola/playback/queue_view.hpp>

#include <memory>

namespace wiola::app {

class PlaybackService {
public:
    PlaybackService(engine::OutputFactory output, std::shared_ptr<media::MediaProvider> provider);
    PlaybackService(engine::OutputFactory output, engine::DecoderOpener open_decoder,
        engine::MetadataReader read_metadata);
    ~PlaybackService();

    PlaybackService(const PlaybackService&) = delete;
    PlaybackService& operator=(const PlaybackService&) = delete;
    PlaybackService(PlaybackService&&) = delete;
    PlaybackService& operator=(PlaybackService&&) = delete;
    void stop_accepting() noexcept;

    engine::OperationReceipt open(std::vector<std::filesystem::path> tracks);
    void add(std::vector<std::filesystem::path> tracks);
    bool remove(std::size_t index);
    void clear();
    bool play_track(std::size_t index);
    bool next_track();
    bool previous_track();

    engine::CommandResult play() noexcept;
    engine::CommandResult pause() noexcept;
    engine::CommandResult resume() noexcept;
    [[nodiscard]] std::optional<engine::Session::MediaReplacementState>
    release_media(const std::filesystem::path& path);
    void restore_media(const engine::Session::MediaReplacementState& state);
    void stop() noexcept;
    void seek(units::Time position) noexcept;

    void set_volume(float gain) noexcept;
    [[nodiscard]] float volume() const noexcept;
    void set_equalizer_enabled(bool enabled) noexcept;
    [[nodiscard]] bool equalizer_enabled() const noexcept;
    void set_equalizer_band(std::size_t index, float db) noexcept;
    [[nodiscard]] float equalizer_band(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t num_equalizer_bands() const noexcept;

    [[nodiscard]] const audio::VolumePolicy& volume_policy() const noexcept;
    [[nodiscard]] const audio::EqualizerConfig& equalizer_config() const noexcept;
    [[nodiscard]] float equalizer_preamp() const noexcept;
    [[nodiscard]] units::Frequency equalizer_band_center(std::size_t index) const noexcept;
    engine::CommandResult toggle_playback() noexcept;
    void seek_to(double fraction) noexcept;
    void cycle_repeat() noexcept;

    /// Drains completions and advances playback; schedulers and headless callers share this pump.
    engine::PlaybackSnapshot pump();
    [[nodiscard]] engine::OperationOutcomes take_outcomes();
    [[nodiscard]] engine::PlaybackSnapshot snapshot() const;
    [[nodiscard]] bool reading() const noexcept;
    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] bool playing() const noexcept;
    [[nodiscard]] engine::OpenResult open_result() const noexcept;
    [[nodiscard]] engine::PlaybackState state() const noexcept;
    [[nodiscard]] units::Time time_played() const noexcept;
    [[nodiscard]] std::optional<units::Time> total_time() const noexcept;
    [[nodiscard]] bool seekable() const noexcept;
    [[nodiscard]] std::vector<std::string> playback_extensions() const;
    [[nodiscard]] const engine::QueueView& playlist() const noexcept;
    [[nodiscard]] const std::filesystem::path& track() const noexcept;

    void set_repeat(engine::RepeatMode repeat) noexcept;
    [[nodiscard]] engine::RepeatMode repeat() const noexcept;
    void shuffle(bool on);
    [[nodiscard]] bool shuffled() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wiola::app
