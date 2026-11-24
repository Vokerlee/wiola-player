/**
 * @file
 * @brief Owned media and output-setup failures at the playback boundary.
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

#include <wiola/media/error.hpp>

#include <string>
#include <variant>

namespace wiola::engine {

/// Device construction/setup failed; this says nothing about the validity of the media.
struct OutputSetupError {
    std::string detail;
};

/// Domain-specific completion failure. Backend diagnostic storage is owned by the value.
using OperationError = std::variant<media::Error, OutputSetupError>;

} // namespace wiola::engine
