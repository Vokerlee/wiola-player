/**
 * @file
 * @brief Another source with the chain applied.
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

#include <wiola/audio/shaped_source.hpp>

#include <algorithm>

namespace wiola::audio {

ShapedSource::ShapedSource(Source& source, ProcessingChain& chain) noexcept
    : source_{source}
    , chain_{chain}
{
}

std::size_t ShapedSource::render(std::span<float> interleaved) noexcept
{
    const std::size_t num_rendered{std::min(source_.render(interleaved), interleaved.size())};

    chain_.process(interleaved.first(num_rendered));

    return num_rendered;
}

pcm::StreamSpec ShapedSource::spec() const noexcept
{
    return source_.spec();
}

} // namespace wiola::audio
