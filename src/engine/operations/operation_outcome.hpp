/**
 * @file
 * @brief Accepted operation identity and bounded asynchronous completion values.
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

#include <media/result.hpp>

#include <cstdint>
#include <vector>

namespace wiola::engine {

/// Stable within an operation kind; zero means there is no asynchronous completion to expect.
using OperationId = std::uint64_t;

enum class OperationKind {
    open,
    metadata,
};

enum class CommandResult {
    accepted,
    unavailable,
};

struct OperationReceipt {
    OperationId id{0};
    OperationKind operation{OperationKind::open};
    CommandResult acceptance{CommandResult::unavailable};
};

struct OperationOutcome {
    OperationId id{0};
    OperationKind operation{OperationKind::open};
    media::Result<void> result;
};

struct OperationOutcomes {
    std::vector<OperationOutcome> values;
    /// Oldest values were discarded before this batch; callers must not treat it as full history.
    bool overflowed{false};
};

} // namespace wiola::engine
