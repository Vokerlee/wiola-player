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
#include <wiola/foundation/owner_thread_lifetime.hpp>
#include <wiola/foundation/prepared_change.hpp>

#include <memory>
#include <optional>

struct Reader {
    explicit Reader(const int& value)
        : value{value}
    {
    }

    int read() const { return value + offset; }

    const int& value;
    int offset{};
    wiola::core::ChangeVersion version;
};

int main()
{
    std::optional<wiola::core::Lease<int>> input = wiola::core::Lease<int>::make(42);
    auto reader = wiola::core::Lease<Reader>::dependent(
        [](int& value) { return std::make_unique<Reader>(value); }, *input
    );
    input.reset();
    if (reader->read() != 42)
        return 1;

    wiola::core::PreparedChange change{reader, &Reader::version, 5,
        [](Reader& owner, int& candidate) noexcept { owner.offset = candidate; }};
    if (!change.commit() || change.commit() || reader->read() != 47)
        return 4;

    wiola::core::OwnerThreadLifetime<Reader>::Observation observation;
    {
        wiola::core::OwnerThreadLifetime scope{reader.get()};
        observation = scope.observe();
        if (observation.get() != &reader.get())
            return 2;
    }

    return observation.get() == nullptr ? 0 : 3;
}
