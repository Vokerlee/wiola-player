/**
 * @file
 * @brief One step of the shaping between a decoder and a device.
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

#include <wiola/media/stream_spec.hpp>

#include <memory>
#include <span>

namespace wiola::audio {

/**
 * One step of the shaping a chain does, run in the order the chain holds them.
 *
 * Shaping happens on the thread that plays, so `process` does what it does and nothing else: no
 * lock to wait on, no allocation, no reaching for a file. What a listener asks for arrives by
 * whatever means the step itself provides, which for the ones here is an atomic.
 */
class Stage {
public:
    Stage() = default;

    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;
    Stage(Stage&&) = delete;
    Stage& operator=(Stage&&) = delete;

    virtual ~Stage() = default;

    /// Owned preparation for one stage or an entire chain. The stage(s) must outlive it.
    /// Destruction without commit discards the candidate and preserves active processing.
    class Preparation {
    public:
        Preparation(const Preparation&) = delete;
        Preparation& operator=(const Preparation&) = delete;
        Preparation(Preparation&&) = delete;
        Preparation& operator=(Preparation&&) = delete;
        virtual ~Preparation() = default;

        /// Control thread, after callbacks have quiesced. No allocation or failure;
        /// repeated commits are inert. Destroy the preparation on the control thread too.
        void commit() noexcept
        {
            if (!committed_) {
                activate();
                committed_ = true;
            }
        }

    protected:
        Preparation() = default;

    private:
        virtual void activate() noexcept = 0;
        bool committed_{false};
    };

    /// Control thread: prepare storage for `spec` without changing active processing.
    /// Existing callbacks may continue. A stateless stage returns null.
    [[nodiscard]] virtual std::unique_ptr<Preparation> prepare(pcm::StreamSpec spec) = 0;

    /// Immediate preparation/commit between streams, with output already stopped.
    /// A failed preparation preserves the current stream and processing history.
    void configure(pcm::StreamSpec spec)
    {
        if (auto prepared{prepare(spec)})
            prepared->commit();
    }

    /// Shapes `samples` in place.
    virtual void process(std::span<float> samples) noexcept = 0;
};

} // namespace wiola::audio
