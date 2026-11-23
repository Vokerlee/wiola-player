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

#include <engine/pipeline/playback_pipeline.hpp>

#include <utility>

namespace wiola::engine {

PlaybackPipeline::PlaybackPipeline(std::unique_ptr<media::AudioDecoder> source,
    audio::Output& output, pcm::Frames buffer_frames, TransportConfig transport)
    : buffer_{source->spec(), buffer_frames}
    , decoded_{source->spec(), buffer_.consumer()}
    , player_{std::move(source), buffer_.producer(), output, transport}
{
}

} // namespace wiola::engine
