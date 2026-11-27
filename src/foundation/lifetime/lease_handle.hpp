/**
 * @file
 * @brief Move-only capabilities backed by retained ownership.
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

#include "lease.hpp"

#include <optional>
#include <utility>

namespace wiola::core {

/// A transferable capability whose owner stays alive. Derived ports own claim/release policy.
template<typename T>
class LeaseHandle {
public:
    LeaseHandle(const LeaseHandle&) = delete;
    LeaseHandle& operator=(const LeaseHandle&) = delete;

    LeaseHandle(LeaseHandle&& other) noexcept
        : owner_{std::exchange(other.owner_, std::nullopt)}
    {
    }

    LeaseHandle& operator=(LeaseHandle&& other) noexcept
    {
        if (this != &other) {
            auto retired = std::exchange(owner_, std::exchange(other.owner_, std::nullopt));
        }
        return *this;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return owner_.has_value(); }

protected:
    LeaseHandle() noexcept = default;

    explicit LeaseHandle(Lease<T> owner) noexcept
        : owner_{owner}
    {
    }

    ~LeaseHandle() = default;

    [[nodiscard]] T* owner() const noexcept
    {
        return owner_ ? std::addressof(owner_->get()) : nullptr;
    }

    [[nodiscard]] std::optional<Lease<T>> retain_owner() const noexcept { return owner_; }

    void clear() noexcept { auto retired = std::exchange(owner_, std::nullopt); }

private:
    std::optional<Lease<T>> owner_;
};

} // namespace wiola::core
