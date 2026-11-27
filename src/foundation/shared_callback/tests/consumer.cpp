/**
 * @file
 * @brief Standard-only consumer of the shared callback component.
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

#include <wiola/foundation/shared_callback.hpp>

#include <memory>

int main()
{
    wiola::core::SharedCallback<int()> original{[next = 0]() mutable { return ++next; }};
    auto retained = original;
    original = {};
    if (original || retained() != 1 || retained() != 2)
        return 1;

    int result = 0;
    wiola::core::SharedCallback<void(std::unique_ptr<int>)> transfer{
        [&](std::unique_ptr<int> value) { result = *value; }};
    transfer(std::make_unique<int>(42));
    return result == 42 ? 0 : 2;
}
