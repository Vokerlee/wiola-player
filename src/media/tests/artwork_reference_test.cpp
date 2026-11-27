/**
 * @file
 * @brief Stable artwork identity and weak-reference lifetime contracts.
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

#include <wiola/media/artwork_reference.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>

namespace {

using wiola::media::Artwork;
using wiola::media::artwork_content_id;
using wiola::media::ArtworkAvailability;
using wiola::media::ArtworkReference;

TEST(ArtworkContentId, EmptyBytesHaveNoIdentityEvenWithOtherFieldsPresent)
{
    const Artwork artwork{.data = {}, .mime_type = "image/png", .content_id = 123};

    EXPECT_EQ(artwork_content_id(artwork), 0U);
}

TEST(ArtworkContentId, HashesBytesAndMimeTypeUsingStableReferenceVectors)
{
    const Artwork byte_only{.data = {std::byte{0x61}}, .mime_type = {}};
    const Artwork with_mime{.data = {std::byte{0x61}}, .mime_type = "bc"};

    // FNV-1a 64-bit golden values for "a" and "abc".
    EXPECT_EQ(artwork_content_id(byte_only), 0xaf63dc4c8601ec8cULL);
    EXPECT_EQ(artwork_content_id(with_mime), 0xe71fa2190541574bULL);

    auto changed = with_mime;
    changed.data.front() = std::byte{0xFF};
    EXPECT_NE(artwork_content_id(changed), artwork_content_id(with_mime));

    changed = with_mime;
    changed.mime_type = "image/png";
    EXPECT_NE(artwork_content_id(changed), artwork_content_id(with_mime));

    changed = with_mime;
    changed.content_id = 123;
    EXPECT_EQ(artwork_content_id(changed), artwork_content_id(with_mime));
}

TEST(ArtworkReference, DistinguishesUnknownMissingAndFailedReferences)
{
    const ArtworkReference unknown;
    const ArtworkReference missing{std::shared_ptr<const Artwork>{}};
    const auto failed = ArtworkReference::failed();

    EXPECT_FALSE(unknown.known());
    EXPECT_EQ(unknown.availability(), ArtworkAvailability::unknown);
    EXPECT_TRUE(missing.known());
    EXPECT_EQ(missing.availability(), ArtworkAvailability::missing);
    EXPECT_TRUE(failed.known());
    EXPECT_EQ(failed.availability(), ArtworkAvailability::failed);

    for (const auto& reference : {unknown, missing, failed}) {
        EXPECT_FALSE(static_cast<bool>(reference));
        EXPECT_FALSE(reference.lock());
        EXPECT_EQ(reference.content_id(), 0U);
    }

    EXPECT_NE(unknown, missing);
    EXPECT_NE(missing, failed);
    EXPECT_EQ(failed, ArtworkReference::failed());
}

TEST(ArtworkReference, EmptyArtworkRemainsMissingDespiteAPreassignedIdentity)
{
    const auto owner =
        std::make_shared<const Artwork>(Artwork{.data = {}, .mime_type = {}, .content_id = 123});
    const ArtworkReference reference{owner};

    EXPECT_TRUE(reference.known());
    EXPECT_EQ(reference.availability(), ArtworkAvailability::missing);
    EXPECT_EQ(reference.content_id(), 0U);
    EXPECT_FALSE(static_cast<bool>(reference));
    EXPECT_EQ(reference.lock(), owner);
}

TEST(ArtworkReference, UsesAStoredIdentityAndBorrowsTheOriginalBytes)
{
    const auto owner = std::make_shared<const Artwork>(
        Artwork{.data = {std::byte{0x61}}, .mime_type = "image/png", .content_id = 123}
    );
    const ArtworkReference reference{owner};

    EXPECT_TRUE(reference.known());
    EXPECT_TRUE(static_cast<bool>(reference));
    EXPECT_EQ(reference.availability(), ArtworkAvailability::available);
    EXPECT_EQ(reference.content_id(), 123U);
    EXPECT_EQ(reference.lock(), owner);
    EXPECT_EQ(owner.use_count(), 1);
}

TEST(ArtworkReference, ComputesAMissingIdentityWithoutChangingTheOwner)
{
    const auto owner =
        std::make_shared<Artwork>(Artwork{.data = {std::byte{0x61}}, .mime_type = {}});
    const ArtworkReference reference{owner};

    EXPECT_TRUE(static_cast<bool>(reference));
    EXPECT_EQ(reference.content_id(), 0xaf63dc4c8601ec8cULL);
    EXPECT_EQ(owner->content_id, 0U);
    EXPECT_EQ(reference.lock().get(), owner.get());
    EXPECT_EQ(owner.use_count(), 1);
}

TEST(ArtworkReference, ExpirationReleasesTheBytesAndRetainsContentIdentity)
{
    auto owner = std::make_shared<Artwork>(Artwork{.data = {std::byte{0x61}}, .mime_type = {}});
    const ArtworkReference reference{owner};
    const auto identity = reference.content_id();

    owner.reset();

    EXPECT_FALSE(reference.lock());
    EXPECT_FALSE(static_cast<bool>(reference));
    EXPECT_TRUE(reference.known());
    EXPECT_EQ(reference.availability(), ArtworkAvailability::available);
    EXPECT_EQ(reference.content_id(), identity);
}

TEST(ArtworkReference, EqualityUsesContentAndAvailabilityInsteadOfPointerIdentity)
{
    const auto first =
        std::make_shared<const Artwork>(Artwork{.data = {std::byte{0x61}}, .mime_type = {}});
    const auto second = std::make_shared<const Artwork>(*first);
    const auto different =
        std::make_shared<const Artwork>(Artwork{.data = {std::byte{0x62}}, .mime_type = {}});

    EXPECT_NE(first.get(), second.get());
    EXPECT_EQ(ArtworkReference{first}, ArtworkReference{second});
    EXPECT_NE(ArtworkReference{first}, ArtworkReference{different});
    EXPECT_NE(ArtworkReference{first}, ArtworkReference{});
}

} // namespace
