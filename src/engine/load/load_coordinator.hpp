/**
 * @file
 * @brief Applicability policy for asynchronous load completion.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
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

#include <engine/load/load_request.hpp>

#include <cstdint>
#include <optional>
#include <utility>

namespace wiola::engine {

/// Single-caller load intent. Physical I/O cancellation remains the worker's separate concern.
class LoadCoordinator {
public:
    [[nodiscard]] LoadRequest begin(QueueEntryId entry, std::filesystem::path path,
        LoadIntent intent)
    {
        ++generation_;
        active_ = LoadRequest{.generation = generation_,
            .entry = entry,
            .path = std::move(path),
            .intent = intent};
        return *active_;
    }

    void invalidate() noexcept
    {
        ++generation_;
        active_.reset();
    }

    void invalidate_if(QueueEntryId entry) noexcept
    {
        if (active_ && active_->entry == entry)
            invalidate();
    }

    /// Accepts a completion at most once, and only for the exact active request.
    [[nodiscard]] bool accept(const LoadCompletion& completion) noexcept
    {
        if (!active_ || completion.request != *active_)
            return false;

        active_.reset();
        return true;
    }

private:
    std::uint64_t generation_{0};
    std::optional<LoadRequest> active_;
};

} // namespace wiola::engine
