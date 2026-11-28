/**
 * @file
 * @brief A chain composed the way a session composes one.
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

#include <wiola/audio/equalizer.hpp>
#include <wiola/audio/processing_chain.hpp>
#include <wiola/audio/volume.hpp>
#include <wiola/media/stream_spec.hpp>

namespace wiola::testing {

/// The steps a session puts a track through, in the same order, with each of them to hand: the
/// bands first, then how loud, so that what a listener asks for last is done last.
struct Shaping {
    explicit Shaping(pcm::StreamSpec spec, audio::BandLayout layout = audio::BandLayout{})
        : equalizer{core::Lease<audio::Equalizer>::make(spec, layout)}
        , chain{core::Lease<audio::ProcessingChain>::make(
              std::initializer_list<core::Lease<audio::Stage>>{equalizer, volume}
          )}
    {
    }

    core::Lease<audio::Equalizer> equalizer;
    core::Lease<audio::Volume> volume = core::Lease<audio::Volume>::make();
    core::Lease<audio::ProcessingChain> chain;
};

} // namespace wiola::testing
