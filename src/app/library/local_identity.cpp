/**
 * @file
 * @brief Local-path compatibility for persisted catalog identities.
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

#include "local_identity.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace wiola::app {

namespace {

bool case_insensitive(LocalPathCase policy) noexcept
{
#ifdef _WIN32
    constexpr auto native = LocalPathCase::insensitive;
#else
    constexpr auto native = LocalPathCase::sensitive;
#endif
    return (policy == LocalPathCase::native ? native : policy) == LocalPathCase::insensitive;
}

void fold_ascii_case(std::string& path) noexcept
{
    std::ranges::transform(path, path.begin(), [](unsigned char character) {
        return static_cast<char>(character >= 'A' && character <= 'Z' ? character + ('a' - 'A')
                                                                      : character);
    });
}

} // namespace

library::MediaId media_id_for(const std::filesystem::path& location, LocalPathCase case_policy)
{
    if (location.empty())
        return {};

    const std::filesystem::path absolute{std::filesystem::absolute(location)};
    std::string normalized{absolute.lexically_normal().generic_string()};

    if (case_insensitive(case_policy))
        fold_ascii_case(normalized);

    return {.value = std::move(normalized)};
}

} // namespace wiola::app
