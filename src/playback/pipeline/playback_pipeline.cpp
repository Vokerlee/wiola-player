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

#include "playback_pipeline.hpp"

#include <utility>

namespace wiola::engine {

PlaybackPipeline::PlaybackPipeline(std::unique_ptr<media::AudioDecoder> source,
    std::unique_ptr<OutputSession::Attachment> output, pcm::Frames buffer_frames,
    TransportConfig transport)
    : buffer_{core::Lease<pcm::PcmQueue>::make(source->spec(), buffer_frames)}
    , decoded_{core::Lease<audio::BufferSource>::dependent(
          [spec = source->spec()](pcm::PcmQueue& queue) {
              return std::make_unique<audio::BufferSource>(spec, queue.consumer());
          },
          buffer_
      )}
    , output_{std::move(output)}
    , player_{std::move(source), buffer_->producer(), output_->output(), transport}
{
}

bool PlaybackPipeline::activate(OutputSession& output,
    std::optional<audio::Output::Quiescence> previous) noexcept
{
    if (player_.state() != PlaybackState::idle)
        return false;

    auto permission = output_->take_quiescence();
    if (!permission && previous && previous->matches(output_->output()))
        permission = std::move(previous);
    if (!permission)
        permission = player_.quiesce_output();
    if (!permission)
        return false;

    return output.activate(*output_, decoded_, *permission) &&
        player_.adopt_output(std::move(*permission));
}

std::optional<audio::Output::Quiescence> PlaybackPipeline::quiesce_output() noexcept
{
    return player_.quiesce_output();
}

} // namespace wiola::engine
