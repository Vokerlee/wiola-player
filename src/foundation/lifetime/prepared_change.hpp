/**
 * @file
 * @brief Owned candidates with version-checked, allocation-free commit.
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

#include <concepts>
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace wiola::core {

template<typename Owner, typename Candidate>
class PreparedChange;

/// Owner-thread identity of the last committed change; prepared candidates do not advance it.
class ChangeVersion final {
    template<typename, typename>
    friend class PreparedChange;

public:
    ChangeVersion() noexcept = default;
    ChangeVersion(const ChangeVersion&) = delete;
    ChangeVersion& operator=(const ChangeVersion&) = delete;
    ChangeVersion(ChangeVersion&&) = delete;
    ChangeVersion& operator=(ChangeVersion&&) = delete;

private:
    struct Stamp { };

    std::shared_ptr<const Stamp> stamp_;
    bool applying_{false};
};

/// Retains both owner and candidate. Setup may allocate; commit allocates and throws nothing.
/// All operations belong to the owner's control thread, outside real-time render callbacks.
template<typename Owner, typename Candidate>
class PreparedChange final {
    using VersionMember = ChangeVersion Owner::*;
    using Apply = std::move_only_function<void(Owner&, Candidate&) noexcept>;

    struct State {
        template<typename Input, typename Operation>
        State(Lease<Owner> retained, VersionMember member, Input&& input, Operation&& operation)
            : owner{retained}
            , version{checked(member)}
            , expected{(owner.get().*version).stamp_}
            , next{std::make_shared<const ChangeVersion::Stamp>()}
            , candidate{std::forward<Input>(input)}
            , apply{checked_operation(Apply{std::forward<Operation>(operation)})}
        {
        }

        static Apply checked_operation(Apply operation)
        {
            if (!operation)
                throw std::invalid_argument{"prepared change requires an apply operation"};
            return operation;
        }

        [[nodiscard]] bool ready() const noexcept
        {
            const auto& current = owner.get().*version;
            return !committed && !current.applying_ && expected == current.stamp_;
        }

        static VersionMember checked(VersionMember member)
        {
            if (!member)
                throw std::invalid_argument{"prepared change requires an owner version member"};
            return member;
        }

        // Reverse destruction retires candidate/callable before releasing their owner.
        Lease<Owner> owner;
        VersionMember version;
        std::shared_ptr<const ChangeVersion::Stamp> expected;
        std::shared_ptr<const ChangeVersion::Stamp> next;
        Candidate candidate;
        Apply apply;
        bool committed{false};
    };

public:
    template<typename Input, typename Operation>
        requires std::constructible_from<Candidate, Input&&> &&
        std::is_nothrow_invocable_r_v<void, Operation&, Owner&, Candidate&>
    PreparedChange(Lease<Owner> owner, VersionMember version, Input&& candidate, Operation&& apply)
        : state_{Lease<State>::make(owner, version, std::forward<Input>(candidate),
              std::forward<Operation>(apply))}
    {
    }

    PreparedChange(const PreparedChange&) = delete;
    PreparedChange& operator=(const PreparedChange&) = delete;
    PreparedChange(PreparedChange&&) = delete;
    PreparedChange& operator=(PreparedChange&&) = delete;

    [[nodiscard]] bool ready() const noexcept { return state_->ready(); }

    [[nodiscard]] const Candidate& candidate() const & noexcept { return state_->candidate; }

    const Candidate& candidate() const && = delete;

    /// False after a prior commit, an intervening commit, or during a reentrant apply.
    /// Retaining active state also permits the outer transaction to be disposed by its apply.
    bool commit() noexcept
    {
        const auto active = state_;
        if (!active->ready())
            return false;

        active->committed = true;
        auto& version = active->owner.get().*active->version;
        version.applying_ = true;
        active->apply(active->owner.get(), active->candidate);
        version.stamp_.swap(active->next);
        version.applying_ = false;
        return true;
    }

private:
    Lease<State> state_;
};

template<typename Owner, typename Candidate, typename Operation>
PreparedChange(Lease<Owner>, ChangeVersion Owner::*, Candidate&&, Operation&&)
    -> PreparedChange<Owner, std::remove_cvref_t<Candidate>>;

} // namespace wiola::core
