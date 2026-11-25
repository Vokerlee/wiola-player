/**
 * @file
 * @brief Strongly typed identities that can survive playlist and application boundaries.
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

#include <compare>
#include <cstddef>
#include <functional>
#include <string>

namespace wiola::library {

/// An opaque persisted value. Each domain gets a different, non-convertible C++ type.
template<typename Domain>
struct Identity {
    std::string value;

    [[nodiscard]] constexpr bool valid() const noexcept { return !value.empty(); }

    auto operator<=>(const Identity&) const = default;
};

struct MediaIdentity;
struct PlaylistEntryIdentity;
struct PlaylistIdentity;
struct WorkspaceIdentity;
struct TagIdentity;

using MediaId = Identity<MediaIdentity>;
using PlaylistEntryId = Identity<PlaylistEntryIdentity>;
using PlaylistId = Identity<PlaylistIdentity>;
using WorkspaceId = Identity<WorkspaceIdentity>;
using TagId = Identity<TagIdentity>;

} // namespace wiola::library

namespace std {

template<typename Domain>
struct hash<wiola::library::Identity<Domain>> {
    [[nodiscard]] size_t operator()(const wiola::library::Identity<Domain>& identity) const noexcept
    {
        return hash<string>{}(identity.value);
    }
};

} // namespace std
