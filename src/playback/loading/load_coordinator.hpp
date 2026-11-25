/**
 * @file
 * @brief Applicability policy for asynchronous load completion.
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

#include <loading/load_request.hpp>

#include <cstdint>
#include <optional>
#include <utility>

namespace wiola::engine {

/// Single-caller load intent. Physical I/O cancellation remains the worker's separate concern.
class LoadCoordinator {
public:
    [[nodiscard]] LoadRequest begin(QueueEntryId entry, media::ResourceReference resource,
        LoadIntent intent, std::optional<units::Time> position = {})
    {
        ++generation_;
        active_ = LoadRequest{.generation = generation_,
            .entry = entry,
            .resource = std::move(resource),
            .intent = intent,
            .position = position};
        return *active_;
    }

    void invalidate() noexcept
    {
        ++generation_;
        active_.reset();
    }

    [[nodiscard]] bool invalidate_if(QueueEntryId entry) noexcept
    {
        if (!active_ || active_->entry != entry)
            return false;
        invalidate();
        return true;
    }

    /// Retires only an intent for this resource, returning its owned restoration intent.
    [[nodiscard]] std::optional<LoadRequest>
    invalidate_resource(const media::ResourceReference& resource) noexcept
    {
        if (!active_ || active_->resource != resource)
            return {};
        auto retired{std::move(active_)};
        invalidate();
        return retired;
    }

    [[nodiscard]] bool pending() const noexcept { return active_.has_value(); }

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
