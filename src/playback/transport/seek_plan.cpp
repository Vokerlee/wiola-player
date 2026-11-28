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

#include "seek_plan.hpp"

namespace wiola::engine {

SeekPlan SeekPlan::begin(SeekMailbox::Claim claim, bool rewind, bool terminal) noexcept
{
    if (terminal)
        return {Action::cancelled, claim, std::nullopt};

    const auto target = claim.outstanding ? std::optional{claim.target}
        : rewind                          ? std::optional{pcm::Frames{}}
                                          : std::nullopt;

    return {Action::prime, claim, target};
}

SeekPlan SeekPlan::pending(SeekMailbox::Claim claim, bool terminal) noexcept
{
    if (!claim.outstanding)
        return {Action::none, claim, std::nullopt};

    return begin(claim, false, terminal);
}

std::optional<SeekOutcome> SeekPlan::interruption(bool terminal, bool newer_request) noexcept
{
    if (terminal)
        return SeekOutcome::cancelled;

    if (newer_request)
        return SeekOutcome::superseded;

    return std::nullopt;
}

} // namespace wiola::engine
