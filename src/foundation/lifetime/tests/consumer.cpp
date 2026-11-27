/**
 * @file
 * @brief Standard-only consumer of a retained dependency graph.
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

#include <wiola/foundation/lease.hpp>

#include <memory>
#include <optional>

struct Reader {
    explicit Reader(const int& value)
        : value{value}
    {
    }

    int read() const { return value; }

    const int& value;
};

int main()
{
    std::optional<wiola::core::Lease<int>> input = wiola::core::Lease<int>::make(42);
    auto reader = wiola::core::Lease<Reader>::dependent(
        [](int& value) { return std::make_unique<Reader>(value); }, *input
    );
    input.reset();
    return reader->read() == 42 ? 0 : 1;
}
