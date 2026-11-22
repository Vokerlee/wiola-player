/**
 * @file
 * @brief Owned artwork identity and weak presentation references.
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
#include "media.hpp"
#include <memory>

namespace wiola::media {

/// Stable content identity, independent of track/tag revisions. Compute once at the read boundary.
[[nodiscard]] inline std::uint64_t artwork_content_id(const Artwork& artwork) noexcept
{
    if (artwork.empty())
        return 0;
    std::uint64_t value = 14695981039346656037ULL;
    for (const auto byte : artwork.data) {
        value ^= std::to_integer<unsigned char>(byte);
        value *= 1099511628211ULL;
    }
    for (const auto byte : artwork.mime_type) {
        value ^= static_cast<unsigned char>(byte);
        value *= 1099511628211ULL;
    }
    return value == 0 ? 1 : value;
}
enum class ArtworkAvailability {
    unknown,
    missing,
    available,
    failed,
};

/// Presentation references never pin an unbounded number of encoded covers in row snapshots.
class ArtworkReference final {
public:
    ArtworkReference() = default;

    ArtworkReference(std::shared_ptr<const Artwork> artwork)
        : bytes_{artwork}
        , content_{artwork && !artwork->empty()
                  ? (artwork->content_id ? artwork->content_id : artwork_content_id(*artwork))
                  : 0}
        , availability_{content_ ? ArtworkAvailability::available : ArtworkAvailability::missing}
    {
    }

    ArtworkReference(std::shared_ptr<Artwork> artwork)
        : ArtworkReference{std::shared_ptr<const Artwork>{std::move(artwork)}}
    {
    }

    [[nodiscard]] static ArtworkReference failed()
    {
        ArtworkReference result;
        result.availability_ = ArtworkAvailability::failed;
        return result;
    }

    [[nodiscard]] std::shared_ptr<const Artwork> lock() const { return bytes_.lock(); }

    [[nodiscard]] std::uint64_t content_id() const { return content_; }

    [[nodiscard]] ArtworkAvailability availability() const { return availability_; }

    [[nodiscard]] bool known() const { return availability_ != ArtworkAvailability::unknown; }

    [[nodiscard]] explicit operator bool() const
    {
        return !bytes_.expired() && availability_ == ArtworkAvailability::available;
    }

    bool operator==(const ArtworkReference& other) const
    {
        return content_ == other.content_ && availability_ == other.availability_;
    }

private:
    std::weak_ptr<const Artwork> bytes_;
    std::uint64_t content_{};
    ArtworkAvailability availability_{ArtworkAvailability::unknown};
};

} // namespace wiola::media
