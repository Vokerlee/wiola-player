/**
 * @file
 * @brief Bounded shared decoding, negative caching, scaling and themed placeholders.
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

#include "artwork_presentation.hpp"
#include "theme.hpp"

#include <QApplication>
#include <QBuffer>
#include <QImageReader>
#include <QPainter>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wiola::gui {
namespace {

QImage render_cover(const QImage& source, int physical)
{
    const auto size = source.size().scaled(physical, physical, Qt::KeepAspectRatio);

    if (source.size() == size)
        return source;

    QImage result{size, QImage::Format_ARGB32_Premultiplied};
    result.fill(Qt::transparent);

    QPainter painter{&result};
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Thumbnail rendering stays on its owner thread. Small clipped strips avoid fanning a
    // single GUI cover out to the process-wide Qt raster pool.
    for (int top = 0; top < result.height(); top += 32) {
        painter.setClipRect(0, top, result.width(), std::min(32, result.height() - top));
        painter.drawImage(result.rect(), source);
    }

    return result;
}

} // namespace

bool ArtworkRequest::valid() const
{
    return cover_size > 0 && cover_size <= 512 && std::isfinite(dpr) && dpr >= 0 && dpr <= 4;
}

ArtworkPresentationService::ArtworkPresentationService(ArtworkPresentationPolicy policy)
    : policy_{policy}
{
    if (!policy.decoded_bytes || !policy.pixmap_bytes || !policy.max_items || !policy.max_pixels ||
        policy.decode_size <= 0 || policy.decode_size > 2048)
        throw std::invalid_argument{"invalid artwork presentation limits"};
}

std::shared_ptr<ArtworkPresentationService> ArtworkPresentationService::acquire()
{
    Q_ASSERT(QApplication::instance() &&
        QThread::currentThread() == QApplication::instance()->thread());

    // Non-owning process registry: the last GUI owner destroys QPixmaps before QApplication.
    static std::weak_ptr<ArtworkPresentationService> current;
    auto held = current.lock();

    if (!held) {
        held = std::make_shared<ArtworkPresentationService>();
        current = held;
    }

    return held;
}

void ArtworkPresentationService::trim_images(std::size_t incoming)
{
    while (!images_.empty() &&
        (images_.size() >= policy_.max_items ||
            statistics_.decoded_bytes + incoming > policy_.decoded_bytes)) {
        const auto oldest = std::ranges::min_element(images_, {},
            [](const auto& pair) { return pair.second.touched; });
        statistics_.decoded_bytes -= oldest->second.bytes;
        images_.erase(oldest);
        ++statistics_.evictions;
    }
}

void ArtworkPresentationService::trim_pixmaps(std::size_t incoming)
{
    while (!pixmaps_.empty() &&
        (pixmaps_.size() >= policy_.max_items ||
            statistics_.pixmap_bytes + incoming > policy_.pixmap_bytes)) {
        const auto oldest = std::ranges::min_element(pixmaps_, {},
            [](const auto& pair) { return pair.second.touched; });
        statistics_.pixmap_bytes -= oldest->second.bytes;
        pixmaps_.erase(oldest);
        ++statistics_.evictions;
    }
}

QPixmap ArtworkPresentationService::cover(const QWidget& styled, const library::MediaId& media,
    media::ArtworkReference artwork, ArtworkRequest request, std::function<void()> request_source)
{
    Q_ASSERT(QThread::currentThread() == styled.thread());

    if (!request.valid())
        throw std::invalid_argument{"invalid artwork request"};

    const auto dpr =
        request.dpr > 0 ? request.dpr : std::clamp(styled.devicePixelRatioF(), qreal{1}, qreal{4});
    const int physical = static_cast<int>(std::ceil(request.cover_size * dpr));
    auto content = artwork.content_id();

    if (artwork.known() && artwork.availability() != media::ArtworkAvailability::failed)
        latest_[media] = content;
    else if (const auto previous = latest_.find(media); previous != latest_.end())
        content = previous->second;

    if (latest_.size() > policy_.max_items * 4)
        latest_.erase(latest_.begin());

    ContentKey key{media, content};
    PixmapKey positive{key, request.cover_size, static_cast<int>(std::lround(dpr * 1000)), 0};

    if (content)
        if (const auto cached = pixmaps_.find(positive); cached != pixmaps_.end()) {
            cached->second.touched = ++tick_;
            ++statistics_.hits;

            return cached->second.image;
        }

    auto image = images_.find(key);

    if (image != images_.end() && !image->second.image.isNull() &&
        physical > std::max(image->second.image.width(), image->second.image.height()) &&
        image->second.original_size >
            std::max(image->second.image.width(), image->second.image.height()) &&
        artwork) {
        statistics_.decoded_bytes -= image->second.bytes;
        images_.erase(image);
        image = images_.end();
    }

    bool need_source = false;

    if (content && image == images_.end()) {
        const auto encoded = artwork.lock();
        if (encoded && encoded->data.size() <= media::max_encoded_artwork_bytes) {
            ++statistics_.decodes;

            QImage decoded;
            const auto bytes =
                QByteArray::fromRawData(reinterpret_cast<const char*>(encoded->data.data()),
                    static_cast<qsizetype>(encoded->data.size()));
            QBuffer buffer;
            buffer.setData(bytes);
            buffer.open(QIODevice::ReadOnly);

            QImageReader reader{&buffer};
            const auto dimensions = reader.size();

            if (dimensions.isValid() &&
                static_cast<std::uint64_t>(dimensions.width()) *
                        static_cast<std::uint64_t>(dimensions.height()) <=
                    policy_.max_pixels) {
                const int size = std::max(policy_.decode_size, physical);

                decoded = reader.read();

                if (!decoded.isNull() && std::max(decoded.width(), decoded.height()) > size)
                    decoded = render_cover(decoded, size);

                if (!decoded.isNull() &&
                    static_cast<std::size_t>(decoded.sizeInBytes()) > policy_.decoded_bytes)
                    decoded = {};
            }

            const auto cost = static_cast<std::size_t>(decoded.sizeInBytes());
            trim_images(cost);
            image = images_
                        .emplace(key,
                            ImageEntry{std::move(decoded), cost, ++tick_,
                                std::max(dimensions.width(), dimensions.height())})
                        .first;
            statistics_.decoded_bytes += cost;
        } else
            need_source = artwork.availability() != media::ArtworkAvailability::failed;
    } else if (!artwork.known() && image == images_.end())
        need_source = true;

    const bool available = image != images_.end() && !image->second.image.isNull();
    PixmapKey display = positive;

    if (!available)
        display.palette = static_cast<std::uint64_t>(styled.palette().cacheKey());

    if (const auto cached = pixmaps_.find(display); cached != pixmaps_.end()) {
        cached->second.touched = ++tick_;
        ++statistics_.hits;

        const auto result = cached->second.image;

        if (need_source && request_source)
            request_source();

        return result;
    }

    QPixmap result;

    if (available) {
        image->second.touched = ++tick_;
        result =
            QPixmap::fromImage(render_cover(image->second.image, physical), Qt::NoFormatConversion);
    } else
        result = themed_artwork_placeholder(styled, physical);

    result.setDevicePixelRatio(dpr);

    const auto cost =
        static_cast<std::size_t>(result.width()) * static_cast<std::size_t>(result.height()) * 4;

    if (cost <= policy_.pixmap_bytes) {
        trim_pixmaps(cost);
        pixmaps_.emplace(display, PixmapEntry{result, cost, ++tick_});
        statistics_.pixmap_bytes += cost;
    }

    if (need_source && request_source)
        request_source();

    return result;
}

} // namespace wiola::gui
