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

#pragma once

#include <audio/dsp/output_sanitizer.hpp>
#include <audio/dsp/stage.hpp>
#include <core/macros.hpp>
#include <pcm/stream_spec.hpp>

#include <functional>
#include <initializer_list>
#include <span>
#include <vector>

namespace wiola::audio {

/**
 * A fixed sequence of borrowed stages followed by an explicit output sanitizer.
 * The caller owns every stage and stops output before destruction or stream reconfiguration.
 */
class ProcessingChain {
public:
    ProcessingChain(std::initializer_list<std::reference_wrapper<Stage>> stages = {});

    NO_COPY_SEMANTIC(ProcessingChain);
    NO_MOVE_SEMANTIC(ProcessingChain);

    /// Retunes the fixed stages between tracks, never during a callback.
    void configure(pcm::StreamSpec spec);

    /// Runs the fixed topology and sanitizer without allocating, locking, or blocking.
    void process(std::span<float> samples) noexcept;

private:
    std::vector<Stage*> stages_;
    OutputSanitizer sanitizer_;
};

} // namespace wiola::audio
