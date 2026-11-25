/**
 * @file
 * @brief Owner-thread durability acknowledgments and command acceptance results.
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
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace wiola::library {

struct DurabilityEvent {
    std::uint64_t revision;
    bool durable;
};

/// Owner-thread acknowledgment for one accepted revision. Failed attempts retain dirty state.
class CommitReceipt final {
public:
    using Observer = std::function<void(DurabilityEvent)>;

    explicit CommitReceipt(std::uint64_t revision);
    ~CommitReceipt();
    CommitReceipt(const CommitReceipt&) = delete;
    CommitReceipt& operator=(const CommitReceipt&) = delete;
    CommitReceipt(CommitReceipt&&) = delete;
    CommitReceipt& operator=(CommitReceipt&&) = delete;

    void observe(Observer observer);
    /// Deliver to every eligible observer, then rethrow the first callback exception.
    /// Success is terminal; failed attempts retain observers for a later retry.
    void notify(bool durable);
    void cancel();

    [[nodiscard]] bool durable() const noexcept;

private:
    struct State;
    const std::shared_ptr<State> state_;
};
enum class CommitState {
    failed,
    accepted,
    durable,
};

struct CommitResult {
    CommitState state{CommitState::failed};
    std::uint64_t revision{};
    std::shared_ptr<CommitReceipt> receipt;
    CommitResult() = default;

    // Synchronous persistence policies keep their durable bool contract.
    CommitResult(bool success)
        : state{success ? CommitState::durable : CommitState::failed}
    {
    }

    [[nodiscard]] static CommitResult accepted(std::uint64_t revision,
        std::shared_ptr<CommitReceipt> receipt)
    {
        CommitResult result;
        result.state = CommitState::accepted;
        result.revision = revision;
        result.receipt = std::move(receipt);
        return result;
    }

    operator bool() const noexcept { return state != CommitState::failed; }

    [[nodiscard]] bool durable() const noexcept { return state == CommitState::durable; }
};

} // namespace wiola::library
