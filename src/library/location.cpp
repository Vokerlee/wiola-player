/**
 * @file
 * @brief Normalized persistent identities for media locations.
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

#include "location.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace wiola::library {

namespace {

bool case_insensitive(MediaPathCase policy) noexcept
{
    switch (policy) {
    case MediaPathCase::sensitive:
        return false;

    case MediaPathCase::insensitive:
        return true;

    case MediaPathCase::native:
#ifdef _WIN32
        return true;
#else
        return false;
#endif
    }

    return false;
}

void fold_ascii_case(std::string& path) noexcept
{
    std::ranges::transform(path, path.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
}

} // namespace

MediaId media_id_for(const std::filesystem::path& location, MediaPathCase case_policy)
{
    if (location.empty())
        return {};

    const std::filesystem::path absolute{std::filesystem::absolute(location)};
    std::string normalized{absolute.lexically_normal().generic_string()};

    if (case_insensitive(case_policy))
        fold_ascii_case(normalized);

    return {.value = std::move(normalized)};
}

} // namespace wiola::library
