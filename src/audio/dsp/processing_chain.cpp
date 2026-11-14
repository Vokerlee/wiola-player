/**
 * @file
 * @brief Fixed processing topology and final output sanitizing.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
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

#include <audio/dsp/processing_chain.hpp>

namespace wiola::audio {

ProcessingChain::ProcessingChain(std::initializer_list<std::reference_wrapper<Stage>> stages)
{
    stages_.reserve(stages.size());
    for (const auto stage : stages)
        stages_.push_back(&stage.get());
}

void ProcessingChain::configure(pcm::StreamSpec spec)
{
    for (Stage* stage : stages_)
        stage->configure(spec);
}

void ProcessingChain::process(std::span<float> samples) noexcept
{
    for (Stage* stage : stages_)
        stage->process(samples);

    sanitizer_.process(samples);
}

} // namespace wiola::audio
