/**
 * @file
 * @brief Playback permutation, cursor, and repeat policy.
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

#include <queue/playback_order.hpp>

#include <algorithm>
#include <numeric>
#include <random>

namespace wiola::engine {

void PlaybackOrder::reset(std::size_t size)
{
    order_.resize(size);
    std::iota(order_.begin(), order_.end(), std::size_t{0});
    shuffled_ = false;
    place_ = order_.empty() ? std::nullopt : std::optional<std::size_t>{0};
}

void PlaybackOrder::reserve(std::size_t size)
{
    order_.reserve(size);
}

void PlaybackOrder::add()
{
    order_.push_back(order_.size());

    if (!place_)
        place_ = 0;
}

bool PlaybackOrder::remove(std::size_t index) noexcept
{
    if (index >= order_.size())
        return false;

    const std::optional<std::size_t> standing{position()};
    order_.erase(std::ranges::find(order_, index));

    for (std::size_t& entry : order_)
        if (entry > index)
            --entry;

    if (order_.empty()) {
        place_.reset();
        return true;
    }

    if (!standing) {
        place_ = 0;
        return true;
    }

    if (*standing == index) {
        place_ = place_of(std::min(index, order_.size() - 1));
        return true;
    }

    place_ = place_of(*standing > index ? *standing - 1 : *standing);
    return true;
}

void PlaybackOrder::clear() noexcept
{
    order_.clear();
    place_.reset();
    shuffled_ = false;
}

std::optional<std::size_t> PlaybackOrder::position() const noexcept
{
    if (!place_)
        return std::nullopt;

    return order_[*place_];
}

bool PlaybackOrder::go_to(std::size_t index) noexcept
{
    if (index >= order_.size())
        return false;

    place_ = place_of(index);
    return true;
}

bool PlaybackOrder::next() noexcept
{
    if (!place_)
        return false;

    if (repeat_ == Repeat::track)
        return true;

    if (*place_ + 1 < order_.size()) {
        place_ = *place_ + 1;
        return true;
    }

    if (repeat_ != Repeat::all)
        return false;

    place_ = 0;
    return true;
}

bool PlaybackOrder::previous() noexcept
{
    if (!place_)
        return false;

    if (repeat_ == Repeat::track)
        return true;

    if (*place_ > 0) {
        place_ = *place_ - 1;
        return true;
    }

    if (repeat_ != Repeat::all)
        return false;

    place_ = order_.size() - 1;
    return true;
}

void PlaybackOrder::shuffle(std::uint_fast32_t seed)
{
    const std::optional<std::size_t> standing{position()};
    std::mt19937 random{seed};
    std::ranges::shuffle(order_, random);
    shuffled_ = true;

    if (standing)
        place_ = place_of(*standing);
}

void PlaybackOrder::unshuffle() noexcept
{
    const std::optional<std::size_t> standing{position()};
    std::iota(order_.begin(), order_.end(), std::size_t{0});
    shuffled_ = false;

    if (standing)
        place_ = place_of(*standing);
}

std::size_t PlaybackOrder::place_of(std::size_t index) const noexcept
{
    const auto found = std::ranges::find(order_, index);
    return static_cast<std::size_t>(std::distance(order_.begin(), found));
}

} // namespace wiola::engine
