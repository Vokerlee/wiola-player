/**
 * @file
 * @brief Owner-thread durability acknowledgments with isolated observer failures.
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

#include "commit_receipt.hpp"
#include <exception>
#include <stdexcept>
#include <thread>
#include <vector>

namespace wiola::library {

struct CommitReceipt::State {
    enum class Outcome {
        pending,
        failed,
        durable,
    };

    explicit State(std::uint64_t accepted_revision)
        : revision{accepted_revision}
    {
    }

    void check_owner(const char* message) const
    {
        if (std::this_thread::get_id() != owner)
            throw std::logic_error{message};
    }

    void cancel() noexcept
    {
        if (std::exchange(cancelled, true))
            return;
        observers.clear();
    }

    const std::thread::id owner{std::this_thread::get_id()};
    const std::uint64_t revision;
    std::vector<std::shared_ptr<Observer>> observers;
    Outcome outcome{Outcome::pending};
    bool cancelled{};
};

CommitReceipt::CommitReceipt(std::uint64_t revision)
    : state_{std::make_shared<State>(revision)}
{
}

CommitReceipt::~CommitReceipt()
{
    state_->cancel();
}

bool CommitReceipt::durable() const noexcept
{
    return state_->outcome == State::Outcome::durable;
}

void CommitReceipt::observe(Observer observer)
{
    const auto state = state_;
    state->check_owner("commit receipt accessed outside owner thread");
    if (!observer || state->cancelled)
        return;
    if (state->outcome == State::Outcome::durable) {
        observer({state->revision, true});
        return;
    }
    const auto retained = std::make_shared<Observer>(std::move(observer));
    state->observers.push_back(retained);
    if (state->outcome == State::Outcome::failed)
        (*retained)({state->revision, false});
}

void CommitReceipt::notify(bool durable)
{
    const auto state = state_;
    state->check_owner("commit receipt resolved outside owner thread");
    if (state->outcome == State::Outcome::durable || state->cancelled)
        return;
    state->outcome = durable ? State::Outcome::durable : State::Outcome::failed;
    auto observers = durable ? std::move(state->observers) : state->observers;
    std::exception_ptr failure;
    for (const auto& observer : observers) {
        if (state->cancelled || (!durable && state->outcome == State::Outcome::durable))
            break;
        try {
            (*observer)({state->revision, durable});
        } catch (...) {
            if (!failure)
                failure = std::current_exception();
        }
    }
    if (failure)
        std::rethrow_exception(failure);
}

void CommitReceipt::cancel()
{
    const auto state = state_;
    state->check_owner("commit receipt cancelled outside owner thread");
    state->cancel();
}

} // namespace wiola::library
