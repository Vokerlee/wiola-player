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

#include "allocation_failure.hpp"

#include <cstdlib>
#include <limits>
#include <new>
#include <utility>

namespace {

thread_local std::size_t allocations_before_failure{std::numeric_limits<std::size_t>::max()};

}

namespace wiola::testing {

AllocationFailure::AllocationFailure(std::size_t after) noexcept
    : previous_{std::exchange(allocations_before_failure, after)}
{
}

AllocationFailure::~AllocationFailure()
{
    allocations_before_failure = previous_;
}

} // namespace wiola::testing

// Standard replacement functions keep fault injection local to this test executable and
// independent of platform-specific mangled symbols or linker wrapping support.
void* operator new(std::size_t size)
{
    if (allocations_before_failure == 0)
        throw std::bad_alloc{};
    if (allocations_before_failure != std::numeric_limits<std::size_t>::max())
        --allocations_before_failure;

    while (true) {
        if (void* memory{std::malloc(size == 0 ? 1 : size)})
            return memory;
        const auto handler{std::get_new_handler()};
        if (!handler)
            throw std::bad_alloc{};
        handler();
    }
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept
{
    return ::operator new(size, tag);
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    ::operator delete(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    ::operator delete(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    ::operator delete(memory);
}

void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    ::operator delete(memory);
}

void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
    ::operator delete(memory);
}
