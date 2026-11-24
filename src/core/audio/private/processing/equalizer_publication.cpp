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

#include <processing/equalizer_publication.hpp>

namespace wiola::audio {

void EqualizerPublication::publish(const Bank& bank) noexcept
{
    banks_[write_slot_] = bank;
    // Acquire the released slot before reusing it; publish completed writes to the reader.
    write_slot_ =
        exchange_slot_.exchange(write_slot_ | pending_flag, std::memory_order_acq_rel) & slot_mask;
}

const EqualizerPublication::Bank& EqualizerPublication::acquire() noexcept
{
    if ((exchange_slot_.load(std::memory_order_acquire) & pending_flag) != 0) {
        // Releasing the former read slot happens after the previous block finished reading it.
        // A writer racing this load may publish a newer bank; exchange adopts that whole bank.
        read_slot_ = exchange_slot_.exchange(read_slot_, std::memory_order_acq_rel) & slot_mask;
    }
    return banks_[read_slot_];
}

} // namespace wiola::audio
