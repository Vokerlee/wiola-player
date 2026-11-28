/**
 * @file
 * @brief Explicit invalidation of observations on an owner thread.
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

#include <cassert>
#include <memory>
#include <thread>
#include <utility>

namespace wiola::core {

/// Observes an externally owned object on its owner thread; never acquires ownership of T.
/// Invalidate at the owner's teardown boundary before callbacks could observe dismantled state.
template<typename T>
class OwnerThreadLifetime final {
    struct State {
        T* owner;
        const std::thread::id thread{std::this_thread::get_id()};
    };

public:
    class Observation final {
        friend class OwnerThreadLifetime;

    public:
        Observation() noexcept = default;

        /// The result is a short-lived borrow on the owner thread. Recheck after reentrant calls.
        [[nodiscard]] T* get() const noexcept
        {
            if (!state_)
                return nullptr;

            assert((state_->thread == std::this_thread::get_id()) &&
                "access requires the owner thread");
            return state_->owner;
        }

    private:
        explicit Observation(std::shared_ptr<State> state) noexcept
            : state_{std::move(state)}
        {
        }

        std::shared_ptr<State> state_;
    };

    explicit OwnerThreadLifetime(T& owner)
        : state_{std::make_shared<State>(std::addressof(owner))}
    {
    }

    OwnerThreadLifetime(T&&) = delete;

    OwnerThreadLifetime(const OwnerThreadLifetime&) = delete;
    OwnerThreadLifetime& operator=(const OwnerThreadLifetime&) = delete;
    OwnerThreadLifetime(OwnerThreadLifetime&&) = delete;
    OwnerThreadLifetime& operator=(OwnerThreadLifetime&&) = delete;

    ~OwnerThreadLifetime() { invalidate(); }

    /// Safe to copy/transport through workers while this lifetime object remains alive.
    /// Only get() and invalidation access the target, on the original owner thread.
    [[nodiscard]] Observation observe() const noexcept { return Observation{state_}; }

    void invalidate() noexcept
    {
        assert((state_->thread == std::this_thread::get_id()) &&
            "invalidation requires the owner thread");
        state_->owner = nullptr;
    }

private:
    const std::shared_ptr<State> state_;
};

} // namespace wiola::core
