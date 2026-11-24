/**
 * @file
 * @brief A source rendered through a supplied processing stage.
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

#include <wiola/audio/source.hpp>
#include <wiola/audio/stage.hpp>
#include <wiola/media/stream_spec.hpp>

#include <cstddef>
#include <span>

namespace wiola::engine {

/// What another source gives, shaped by the supplied stage. Both outlive it.
/// The host supplies its complete processing policy, commonly a ProcessingChain.
class ShapedSource final : public audio::Source {
public:
    ShapedSource(audio::Source& source, audio::Stage& processing) noexcept;

    std::size_t render(std::span<float> interleaved) noexcept override;

    [[nodiscard]] pcm::StreamSpec spec() const noexcept override;

private:
    audio::Source& source_;
    audio::Stage& processing_;
};

} // namespace wiola::engine
