/**
 * @file
 * @brief Scoped allocation failure for portable C++ rollback tests.
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

#include <cstddef>

namespace wiola::testing {

/// Fail an allocation on this thread, then restore the previous limit when the scope ends.
class AllocationFailure {
public:
    enum class Mode {
        persistent,
        once,
    };

    explicit AllocationFailure(std::size_t after, Mode mode = Mode::persistent) noexcept;
    ~AllocationFailure();
    AllocationFailure(const AllocationFailure&) = delete;
    AllocationFailure& operator=(const AllocationFailure&) = delete;
    AllocationFailure(AllocationFailure&&) = delete;
    AllocationFailure& operator=(AllocationFailure&&) = delete;

private:
    std::size_t previous_;
    Mode previous_mode_;
};

} // namespace wiola::testing
