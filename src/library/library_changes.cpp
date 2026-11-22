/**
 * @file
 * @brief Ordered owner-thread delivery of committed library changes.
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

#include "library_changes.hpp"
#include <cassert>
#include <deque>
#include <exception>
#include <map>
#include <stdexcept>
#include <thread>
#include <utility>

namespace wiola::library {

struct LibraryChanges::State {
    std::map<std::uint64_t, Observer> observers;
    std::deque<LibraryChangeBatch> pending;
    std::uint64_t next{1};
    bool publishing{false};
    const std::thread::id owner{std::this_thread::get_id()};
};

bool LibraryChangeBatch::empty() const noexcept
{
    return media.empty() && removed_media.empty() && playlists.empty() &&
        removed_playlists.empty() && workspaces.empty() && removed_workspaces.empty();
}

LibraryChanges::LibraryChanges()
    : state_{std::make_shared<State>()}
{
}

void LibraryChanges::check_owner() const
{
    assert(state_->owner == std::this_thread::get_id());
}

LibraryChanges::Subscription::Subscription(std::weak_ptr<State> state, std::uint64_t id)
    : state_{std::move(state)}
    , id_{id}
{
}

LibraryChanges::Subscription::~Subscription()
{
    reset();
}

LibraryChanges::Subscription::Subscription(Subscription&& other) noexcept
    : state_{std::move(other.state_)}
    , id_{std::exchange(other.id_, 0)}
{
}

LibraryChanges::Subscription& LibraryChanges::Subscription::operator=(Subscription&& other) noexcept
{
    if (this != &other) {
        reset();
        state_ = std::move(other.state_);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

void LibraryChanges::Subscription::reset() noexcept
{
    if (const auto state = state_.lock()) {
        assert(state->owner == std::this_thread::get_id());
        state->observers.erase(id_);
    }
    state_.reset();
    id_ = 0;
}

LibraryChanges::Subscription LibraryChanges::observe(Observer observer)
{
    check_owner();
    if (!observer)
        throw std::invalid_argument{"a library observer must be callable"};
    const auto id = state_->next++;
    state_->observers.emplace(id, std::move(observer));
    return Subscription{state_, id};
}

void LibraryChanges::publish(LibraryChangeBatch batch)
{
    check_owner();
    const auto state = state_;
    state->pending.push_back(std::move(batch));
    if (state->publishing)
        return;
    state->publishing = true;

    struct Reset {
        bool& flag;

        ~Reset() { flag = false; }
    } reset{state->publishing};

    std::exception_ptr failure;
    while (!state->pending.empty()) {
        auto next = std::move(state->pending.front());
        state->pending.pop_front();
        std::vector<std::uint64_t> receivers;
        for (const auto& [id, observer] : state->observers)
            receivers.push_back(id);
        for (const auto id : receivers) {
            const auto found = state->observers.find(id);
            if (found == state->observers.end())
                continue;
            auto observer = found->second;
            try {
                observer(next);
            } catch (...) {
                if (!failure)
                    failure = std::current_exception();
            }
        }
    }
    if (failure)
        std::rethrow_exception(failure);
}

} // namespace wiola::library
