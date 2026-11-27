/**
 * @file
 * @brief Shared bounded artwork presentation by canonical media, content, size, theme and DPI.
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

#include <wiola/library/identity.hpp>
#include <wiola/media/artwork_reference.hpp>

#include <QImage>
#include <QPixmap>
#include <QWidget>

#include <functional>
#include <map>

namespace wiola::gui {

struct ArtworkRequest {
    int cover_size{20};
    qreal dpr{};
    [[nodiscard]] bool valid() const;
};

struct ArtworkPresentationPolicy {
    std::size_t decoded_bytes{32 * 1024 * 1024}, pixmap_bytes{16 * 1024 * 1024}, max_items{1024};
    std::uint64_t max_pixels{16 * 1024 * 1024};
    int decode_size{512};
};

struct ArtworkPresentationStatistics {
    std::uint64_t decodes{}, hits{}, evictions{};
    std::size_t decoded_bytes{}, pixmap_bytes{};
};

class ArtworkPresentationService final {
public:
    explicit ArtworkPresentationService(ArtworkPresentationPolicy policy = {});
    [[nodiscard]] static std::shared_ptr<ArtworkPresentationService> acquire();
    [[nodiscard]] QPixmap cover(const QWidget& styled, const library::MediaId& media,
        media::ArtworkReference artwork, ArtworkRequest request = {},
        std::function<void()> request_source = {});

    [[nodiscard]] ArtworkPresentationStatistics statistics() const { return statistics_; }

private:
    struct ContentKey {
        library::MediaId media;
        std::uint64_t content;
        auto operator<=>(const ContentKey&) const = default;
    };

    struct PixmapKey {
        ContentKey content;
        int size, dpr;
        std::uint64_t palette;
        auto operator<=>(const PixmapKey&) const = default;
    };

    struct ImageEntry {
        QImage image;
        std::size_t bytes;
        std::uint64_t touched;
        int original_size;
    };

    struct PixmapEntry {
        QPixmap image;
        std::size_t bytes;
        std::uint64_t touched;
    };

    void trim_images(std::size_t incoming);
    void trim_pixmaps(std::size_t incoming);
    ArtworkPresentationPolicy policy_;
    ArtworkPresentationStatistics statistics_;
    std::map<ContentKey, ImageEntry> images_;
    std::map<PixmapKey, PixmapEntry> pixmaps_;
    std::map<library::MediaId, std::uint64_t> latest_;
    std::uint64_t tick_{};
};

} // namespace wiola::gui
