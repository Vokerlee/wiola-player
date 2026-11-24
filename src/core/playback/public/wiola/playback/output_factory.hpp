/**
 * @file
 * @brief Host-supplied construction of an output for a playback source.
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

#include <functional>
#include <memory>

namespace wiola::audio {

class Output;
class Source;

} // namespace wiola::audio

namespace wiola::engine {

/// Called on the control thread with a stable source retained for the returned output's lifetime.
/// Construct a stopped output; null or a non-allocation exception reports output unavailability.
/// Allocation failure propagates with its original exceptional policy.
using OutputFactory = std::function<std::unique_ptr<audio::Output>(audio::Source&)>;

} // namespace wiola::engine
