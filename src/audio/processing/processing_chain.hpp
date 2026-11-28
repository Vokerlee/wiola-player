/**
 * @file
 * @brief Fixed processing topology and final output sanitizing.
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

#include <wiola/audio/output_sanitizer.hpp>
#include <wiola/audio/stage.hpp>
#include <wiola/foundation/lease.hpp>
#include <wiola/media/stream_spec.hpp>

#include <initializer_list>
#include <span>
#include <vector>

namespace wiola::audio {

/**
 * A fixed sequence of retained stages followed by an explicit output sanitizer.
 * The chain retains every stage. Stop output before destruction or stream reconfiguration.
 */
class ProcessingChain final : public Stage {
public:
    ProcessingChain(std::initializer_list<core::Lease<Stage>> stages = {});
    explicit ProcessingChain(std::span<const core::Lease<Stage>> stages);

    ProcessingChain(const ProcessingChain&) = delete;
    ProcessingChain& operator=(const ProcessingChain&) = delete;
    ProcessingChain(ProcessingChain&&) = delete;
    ProcessingChain& operator=(ProcessingChain&&) = delete;

    /// Prepares every stage before any is changed. A later failure discards all candidates.
    [[nodiscard]] std::unique_ptr<Preparation> prepare(pcm::StreamSpec spec) override;

    /// Runs the fixed topology and sanitizer without allocating, locking, or blocking.
    void process(std::span<float> samples) noexcept override;

private:
    std::vector<core::Lease<Stage>> stages_;
    OutputSanitizer sanitizer_;
};

} // namespace wiola::audio
