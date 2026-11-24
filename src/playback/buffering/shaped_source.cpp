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

#include "shaped_source.hpp"

#include <algorithm>

namespace wiola::engine {

ShapedSource::ShapedSource(audio::Source& source, audio::Stage& processing) noexcept
    : source_{source}
    , processing_{processing}
{
}

std::size_t ShapedSource::render(std::span<float> interleaved) noexcept
{
    const std::size_t num_rendered{std::min(source_.render(interleaved), interleaved.size())};

    processing_.process(interleaved.first(num_rendered));

    return num_rendered;
}

pcm::StreamSpec ShapedSource::spec() const noexcept
{
    return source_.spec();
}

} // namespace wiola::engine
