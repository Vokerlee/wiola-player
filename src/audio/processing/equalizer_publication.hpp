/**
 * @file
 * @brief Bounded publication of immutable equalizer banks.
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

#include "biquad.hpp"
#include "equalizer_config.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <type_traits>

namespace wiola::audio {

/// Three slots have distinct owners: the control writer, the processing reader and the exchange.
/// Publish coalesces older pending values. Acquire returns a whole bank valid until the next
/// acquire; only one thread performs each role. Reconfiguration transfers roles only when stopped.
class EqualizerPublication {
public:
    struct Bank {
        std::array<BiquadCoefficients, BandLayout::max_count> coefficients{};
        float preamp{1.0F};
        std::size_t num_bands{0};
        bool shaping{false};
        bool enabled{true};
    };

    EqualizerPublication() = default;
    EqualizerPublication(const EqualizerPublication&) = delete;
    EqualizerPublication& operator=(const EqualizerPublication&) = delete;
    EqualizerPublication(EqualizerPublication&&) = delete;
    EqualizerPublication& operator=(EqualizerPublication&&) = delete;

    /// Control thread: copy fixed-size prepared values, then exchange one slot.
    void publish(const Bank& bank) noexcept;

    /// Processing thread: one load and at most one exchange; no software retry or reclamation.
    [[nodiscard]] const Bank& acquire() noexcept;

private:
    static_assert(std::is_trivially_copyable_v<Bank>);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
    static constexpr std::size_t slot_count{3};
    static constexpr std::size_t pending_flag{std::bit_ceil(slot_count)};
    static constexpr std::size_t slot_mask{pending_flag - 1};

    std::array<Bank, slot_count> banks_{};
    std::size_t write_slot_{0};
    std::size_t read_slot_{1};
    std::atomic<std::size_t> exchange_slot_{2};
};

} // namespace wiola::audio
