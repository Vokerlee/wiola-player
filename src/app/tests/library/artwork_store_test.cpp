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

#include <app/library/artwork_store.hpp>

#include <gtest/gtest.h>

namespace {

std::shared_ptr<wiola::media::Artwork> encoded(std::size_t bytes, char value = 'a')
{
    auto result = std::make_shared<wiola::media::Artwork>();
    result->data.resize(bytes, std::byte{static_cast<unsigned char>(value)});
    result->mime_type = "image/png";
    result->content_id = wiola::media::artwork_content_id(*result);

    return result;
}

TEST(ArtworkStore, ByteBudgetEvictsLeastRecentlyUsedWithoutPinningRows)
{
    wiola::app::ArtworkStore store{8, 3};
    const wiola::library::MediaId a{"a"}, b{"b"}, c{"c"};
    auto original = encoded(4);

    ASSERT_TRUE(store.put(a, original));

    auto reference = store.get(a);

    original.reset();
    ASSERT_TRUE(reference);
    ASSERT_TRUE(store.put(b, encoded(4)));

    static_cast<void>(store.get(a));
    ASSERT_TRUE(store.put(c, encoded(4)));
    EXPECT_TRUE(store.get(a).known());
    EXPECT_FALSE(store.get(b).known());
    EXPECT_LE(store.bytes(), 8);

    store.remove(a);
    EXPECT_FALSE(reference);
    EXPECT_NE(reference.content_id(), 0);
}

TEST(ArtworkStore, ContentIdentityIgnoresPointerChangesAndNegativeEntriesStayBounded)
{
    wiola::app::ArtworkStore store{8, 2};
    const wiola::library::MediaId a{"a"}, b{"b"}, c{"c"};

    EXPECT_TRUE(store.put(a, encoded(4)));

    const auto id = store.get(a).content_id();

    EXPECT_FALSE(store.put(a, encoded(4)));
    EXPECT_EQ(store.get(a).content_id(), id);
    EXPECT_TRUE(store.put(a, encoded(4, 'b')));
    EXPECT_NE(store.get(a).content_id(), id);
    EXPECT_TRUE(store.fail(b));
    EXPECT_EQ(store.get(b).availability(), wiola::media::ArtworkAvailability::failed);
    EXPECT_TRUE(store.put(c, encoded(9)));
    EXPECT_EQ(store.get(c).availability(), wiola::media::ArtworkAvailability::missing);
    EXPECT_LE(store.size(), 2);
    EXPECT_FALSE(store.get(a).known());
    EXPECT_THROW((wiola::app::ArtworkStore{0, 1}), std::invalid_argument);
}

} // namespace
