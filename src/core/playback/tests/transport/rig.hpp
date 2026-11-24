/**
 * @file
 * @brief A player wired the way a session wires one, with the sound card left out.
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

#include <buffering/buffer_source.hpp>
#include <buffering/pcm_queue.hpp>
#include <fakes/output.hpp>
#include <transport/player.hpp>
#include <wiola/audio/source.hpp>
#include <wiola/media/audio_decoder.hpp>
#include <wiola/units.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <utility>

namespace wiola::testing {

/// A player assembly whose output progress the test controls. Start alone cannot drain a track;
/// play_out releases the buffered source after intermediate command/state assertions.
struct Rig {
private:
    class PlaybackGate final : public audio::Source {
    public:
        explicit PlaybackGate(audio::Source& source) noexcept
            : source_{source}
        {
        }

        void allow(bool enabled) noexcept { allowed_.store(enabled, std::memory_order_relaxed); }

        pcm::StreamSpec spec() const noexcept override { return source_.spec(); }

        std::size_t render(std::span<float> samples) noexcept override
        {
            return allowed_.load(std::memory_order_relaxed) ? source_.render(samples) : 0;
        }

    private:
        audio::Source& source_;
        std::atomic<bool> allowed_{false};
    };

public:
    explicit Rig(std::unique_ptr<media::AudioDecoder> source)
        : buffer_{source->spec(),
              *source->spec().checked_frames_at(units::Time{std::chrono::milliseconds{250}})}
        , decoded_{source->spec(), buffer_.consumer()}
        , progress_{decoded_}
        , output{progress_}
        , player{std::move(source), buffer_.producer(), output}
    {
    }

    void play_out() noexcept { progress_.allow(true); }

    void hold_output() noexcept { progress_.allow(false); }

private:
    pcm::PcmQueue buffer_;
    audio::BufferSource decoded_;
    PlaybackGate progress_;

public:
    FakeOutput output;
    engine::Player player;
};

} // namespace wiola::testing
