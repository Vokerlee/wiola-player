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

#include <audio/device/output_connection.hpp>

namespace wiola::audio {

OutputConnection::OutputConnection(Output& output) noexcept
    : control_{output.control()}
{
}

OutputConnection::~OutputConnection()
{
    stop();
}

} // namespace wiola::audio
