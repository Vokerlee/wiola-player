/**
 * @file
 * @brief Owns output control and quiesces callbacks before source teardown.
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

#include <audio/output/output.hpp>
#include <core/macros.hpp>

namespace wiola::audio {

/**
 * The control held for one source's playback through an output.
 *
 * Destruction stops the output and waits for any active callback. Declare this after source
 * ownership so destruction runs in reverse and the connection is quiesced before the source is
 * destroyed. Normal seek/stop paths use the same explicit `stop` operation.
 */
class OutputConnection {
public:
    explicit OutputConnection(Output& output) noexcept;

    NO_COPY_SEMANTIC(OutputConnection);
    NO_MOVE_SEMANTIC(OutputConnection);

    ~OutputConnection();

    [[nodiscard]] Output::Control& control() noexcept { return control_; }

    /// Stops playback and returns only after an active source callback has completed.
    void stop() noexcept { control_.stop(); }

private:
    Output::Control control_;
};

} // namespace wiola::audio
