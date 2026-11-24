/**
 * @file
 * @brief Decoder, frame queue, and transport ownership for one loaded track.
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

#include <audio/dsp/buffer_source.hpp>
#include <audio/output/output.hpp>
#include <core/macros.hpp>
#include <engine/transport/player.hpp>
#include <engine/transport/transport_config.hpp>
#include <pcm/pcm_queue.hpp>
#include <wiola/media/audio_decoder.hpp>
#include <wiola/media/stream_spec.hpp>

#include <memory>

namespace wiola::engine {

/// Owns one decoder-to-output pipeline. Destruction joins transport before releasing its source.
class PlaybackPipeline {
public:
    PlaybackPipeline(std::unique_ptr<media::AudioDecoder> source, audio::Output& output,
        pcm::Frames buffer_frames, TransportConfig transport = {});

    PlaybackPipeline(const PlaybackPipeline&) = delete;
    PlaybackPipeline& operator=(const PlaybackPipeline&) = delete;
    PlaybackPipeline(PlaybackPipeline&&) = delete;
    PlaybackPipeline& operator=(PlaybackPipeline&&) = delete;

    [[nodiscard]] audio::BufferSource& decoded() noexcept { return decoded_; }

    [[nodiscard]] Player& player() noexcept { return player_; }

private:
    pcm::PcmQueue buffer_;
    audio::BufferSource decoded_;
    Player player_;
};

} // namespace wiola::engine
