/**
 * @file
 * @brief Owned provider identity and display hint for a media resource.
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
#include <string>
#include <utility>

namespace wiola::media {

/// The host/provider owns the source namespace and interprets its opaque keys. Core never
/// parses either string as a path or URI. Labels are display hints, not resource identity.
class ResourceReference {
public:
    ResourceReference() = default;

    ResourceReference(std::string source, std::string key, std::string label) noexcept
        : source_{std::move(source)}
        , key_{std::move(key)}
        , label_{std::move(label)}
    {
    }

    [[nodiscard]] const std::string& source() const noexcept { return source_; }

    [[nodiscard]] const std::string& key() const noexcept { return key_; }

    [[nodiscard]] const std::string& label() const noexcept { return label_; }

    [[nodiscard]] bool empty() const noexcept { return source_.empty() && key_.empty(); }

    bool operator==(const ResourceReference& other) const noexcept
    {
        return source_ == other.source_ && key_ == other.key_;
    }

    std::strong_ordering operator<=>(const ResourceReference& other) const noexcept
    {
        if (const auto source = source_ <=> other.source_; source != 0)
            return source;
        return key_ <=> other.key_;
    }

private:
    std::string source_;
    std::string key_;
    std::string label_;
};

} // namespace wiola::media
