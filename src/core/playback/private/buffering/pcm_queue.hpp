/**
 * @file
 * @brief A frame-aligned single-producer/single-consumer PCM queue.
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

#include <buffering/spsc_ring_buffer.hpp>
#include <wiola/media/stream_spec.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

namespace wiola::pcm {

/**
 * Interleaved PCM frames over the generic SPSC ring.
 *
 * Producer blocks must be frame-aligned, and a short push still ends at a frame boundary.
 * Consumers asking for an odd or otherwise misaligned span receive only its complete-frame prefix.
 * The ring's rounded capacity may leave trailing samples unused rather than violating alignment.
 */
class PcmQueue {
    using Ring = lockfree::SPSCRingBuffer<float>;

public:
    class Producer {
    public:
        /// Pushes a frame-aligned block, returning the number of samples accepted.
        std::size_t push(std::span<const float> interleaved) noexcept
        {
            assert(interleaved.size() % num_channels_ == 0);
            return buffer_.push(interleaved.first(std::min(interleaved.size(), space_approx())));
        }

        /// Complete-frame space available, in samples.
        [[nodiscard]] std::size_t space_approx() const noexcept
        {
            return buffer_.space_approx() / num_channels_ * num_channels_;
        }

        void mark_discard() noexcept { buffer_.mark_discard(); }

    private:
        friend class PcmQueue;

        Producer(Ring::Producer buffer, std::size_t num_channels) noexcept
            : buffer_{std::move(buffer)}
            , num_channels_{num_channels}
        {
        }

        Ring::Producer buffer_;
        std::size_t num_channels_;
    };

    class Consumer {
    public:
        /// Pops only the complete-frame prefix that fits in `interleaved`, returning samples.
        std::size_t pop(std::span<float> interleaved) noexcept
        {
            const std::size_t aligned{interleaved.size() / num_channels_ * num_channels_};
            return buffer_.pop(interleaved.first(aligned));
        }

    private:
        friend class PcmQueue;

        Consumer(Ring::Consumer buffer, std::size_t num_channels) noexcept
            : buffer_{std::move(buffer)}
            , num_channels_{num_channels}
        {
        }

        Ring::Consumer buffer_;
        std::size_t num_channels_;
    };

    PcmQueue(StreamSpec spec, Frames minimum_frames)
        : spec_{spec}
        , buffer_{require_capacity(spec, minimum_frames)}
    {
    }

    /// Checked frame-to-sample conversion and power-of-two ring rounding.
    [[nodiscard]] static std::optional<std::size_t> checked_capacity(StreamSpec spec,
        Frames minimum_frames) noexcept
    {
        const std::optional<std::size_t> samples{spec.checked_samples_per(minimum_frames)};
        return samples ? Ring::checked_capacity(*samples) : std::nullopt;
    }

    [[nodiscard]] Producer producer() noexcept
    {
        return Producer{buffer_.producer(), spec_.num_channels};
    }

    [[nodiscard]] Consumer consumer() noexcept
    {
        return Consumer{buffer_.consumer(), spec_.num_channels};
    }

    [[nodiscard]] std::size_t size_approx() const noexcept { return buffer_.size_approx(); }

private:
    [[nodiscard]] static std::size_t require_capacity(StreamSpec spec, Frames minimum_frames)
    {
        const std::optional<std::size_t> capacity{checked_capacity(spec, minimum_frames)};

        if (!capacity)
            throw std::invalid_argument{"invalid PCM queue capacity"};

        return *capacity;
    }

    StreamSpec spec_;
    Ring buffer_;
};

} // namespace wiola::pcm
