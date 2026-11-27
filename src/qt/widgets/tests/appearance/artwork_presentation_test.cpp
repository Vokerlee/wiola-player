/**
 * @file
 * @brief Native semantic theme regression tests.
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

#include <qt/widgets/appearance/artwork_presentation.hpp>
#include <qt/widgets/appearance/theme.hpp>

#include <QApplication>
#include <QBuffer>
#include <gtest/gtest.h>

namespace {

std::shared_ptr<wiola::media::Artwork> image(Qt::GlobalColor color, int size = 32)
{
    QImage pixels{size, size, QImage::Format_ARGB32};
    pixels.fill(color);

    QByteArray bytes;
    QBuffer buffer{&bytes};
    buffer.open(QIODevice::WriteOnly);
    pixels.save(&buffer, "PNG");

    auto artwork = std::make_shared<wiola::media::Artwork>();
    artwork->mime_type = "image/png";
    artwork->data.assign(reinterpret_cast<const std::byte*>(bytes.data()),
        reinterpret_cast<const std::byte*>(bytes.data() + bytes.size()));
    artwork->content_id = wiola::media::artwork_content_id(*artwork);

    return artwork;
}

TEST(ArtworkPresentation, SharedOwnersDecodeOnceAcrossViewsSizesAndUnchangedContent)
{
    QWidget table, inspector;
    auto a = wiola::gui::ArtworkPresentationService::acquire();
    auto b = wiola::gui::ArtworkPresentationService::acquire();

    ASSERT_EQ(a, b);

    const wiola::library::MediaId id{"canonical-id"};
    auto bytes = image(Qt::blue);
    const auto first = a->cover(table, id, bytes, {.cover_size = 20});
    const auto again = b->cover(inspector, id, bytes, {.cover_size = 20});

    EXPECT_EQ(first.cacheKey(), again.cacheKey());

    const auto large = b->cover(inspector, id, bytes, {.cover_size = 96, .dpr = 2});

    EXPECT_EQ(large.devicePixelRatioF(), 2);
    EXPECT_EQ(large.width(), 192);
    EXPECT_EQ(large.toImage().pixelColor(0, 0), QColor{Qt::blue});
    EXPECT_EQ(a->statistics().decodes, 1);
    EXPECT_EQ(a->statistics().decoded_bytes, 32 * 32 * 4);

    auto changed = image(Qt::red);
    const auto red = b->cover(table, id, changed);

    EXPECT_EQ(red.toImage().pixelColor(0, 0), QColor{Qt::red});
    EXPECT_EQ(a->statistics().decodes, 2);
}

TEST(ArtworkPresentation, NegativeDecodeAndPlaceholdersAreBoundedAndPaletteSensitive)
{
    QWidget styled;
    wiola::gui::ArtworkPresentationService service{
        {.decoded_bytes = 4096, .pixmap_bytes = 4096, .max_items = 2, .max_pixels = 16}
    };
    const wiola::library::MediaId id{"a"};
    auto bytes = image(Qt::red, 32);
    const auto first = service.cover(styled, id, bytes);
    const auto again = service.cover(styled, id, bytes);

    EXPECT_EQ(first.cacheKey(), again.cacheKey());
    EXPECT_EQ(service.statistics().decodes, 1);

    auto palette = styled.palette();
    palette.setColor(QPalette::Window, QColor{Qt::black});
    palette.setColor(QPalette::WindowText, QColor{Qt::white});
    styled.setPalette(palette);

    const auto dark = service.cover(styled, id, bytes);

    EXPECT_NE(dark.cacheKey(), first.cacheKey());
    EXPECT_EQ(service.statistics().decodes, 1);

    for (int i = 0; i < 8; ++i)
        static_cast<void>(service.cover(styled, wiola::library::MediaId{std::to_string(i)},
            image(Qt::blue)));

    EXPECT_LE(service.statistics().decoded_bytes, 4096);
    EXPECT_LE(service.statistics().pixmap_bytes, 4096);
    EXPECT_GT(service.statistics().evictions, 0);
    EXPECT_THROW(service.cover(styled, id, bytes, {.cover_size = 0}), std::invalid_argument);
}

TEST(ArtworkPresentation, EvictedSourceCanBeRequestedWithoutReentrantCacheIteratorLifetime)
{
    QWidget widget;
    wiola::gui::ArtworkPresentationService service{
        {.decoded_bytes = 4096, .pixmap_bytes = 4096, .max_items = 1}
    };
    const wiola::library::MediaId id{"a"};
    int requests{};
    const auto request = [&] {
        ++requests;
        static_cast<void>(service.cover(widget, wiola::library::MediaId{"other"},
            std::shared_ptr<const wiola::media::Artwork>{}));
    };

    EXPECT_FALSE(service.cover(widget, id, {}, {}, request).isNull());
    EXPECT_FALSE(service.cover(widget, id, {}, {}, request).isNull());
    EXPECT_EQ(requests, 2);
}

TEST(ArtworkPresentation, LargerDpiRequestsReDecodeInsteadOfEnlargingAnOldThumbnail)
{
    QWidget widget;
    wiola::gui::ArtworkPresentationService service{{.decode_size = 32}};
    auto bytes = image(Qt::green, 256);
    const wiola::library::MediaId id{"a"};
    static_cast<void>(service.cover(widget, id, bytes, {.cover_size = 20}));
    EXPECT_EQ(service.statistics().decodes, 1);

    auto large = service.cover(widget, id, bytes, {.cover_size = 96, .dpr = 2});

    EXPECT_EQ(large.width(), 192);
    EXPECT_EQ(service.statistics().decodes, 2);
}

} // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "minimal");

    QApplication app{argc, argv};

    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
