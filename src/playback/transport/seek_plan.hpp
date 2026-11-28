/**
 * @file
 * @brief A validated, recoverable seek and re-prime operation.
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

#include "seek_mailbox.hpp"

#include <optional>

namespace wiola::engine {

enum class SeekOutcome {
    applied,
    preserved,
    read_failed,
    cancelled,
    superseded,
};

/// A claimed generation and its intended work, with no decoder, device or borrowed state.
class SeekPlan {
public:
    enum class Action {
        none,
        cancelled,
        prime,
    };

    [[nodiscard]] static SeekPlan begin(SeekMailbox::Claim claim, bool rewind,
        bool terminal) noexcept;
    [[nodiscard]] static SeekPlan pending(SeekMailbox::Claim claim, bool terminal) noexcept;
    [[nodiscard]] static std::optional<SeekOutcome> interruption(bool terminal,
        bool newer_request) noexcept;

    [[nodiscard]] Action action() const noexcept { return action_; }

    [[nodiscard]] bool needs_quiescence() const noexcept { return action_ == Action::prime; }

    [[nodiscard]] SeekMailbox::Claim claim() const noexcept { return claim_; }

    [[nodiscard]] std::optional<pcm::Frames> target() const noexcept { return target_; }

private:
    SeekPlan(Action action, SeekMailbox::Claim claim, std::optional<pcm::Frames> target) noexcept
        : action_{action}
        , claim_{claim}
        , target_{target}
    {
    }

    Action action_;
    SeekMailbox::Claim claim_;
    std::optional<pcm::Frames> target_;
};

} // namespace wiola::engine
