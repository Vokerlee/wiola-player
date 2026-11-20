/**
 * @file
 * @brief Custom tag value and validation regression tests.
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

#include <library/tags.hpp>

#include <gtest/gtest.h>

namespace {

TEST(TagValue, TrimsInputAndPreservesDisplayCasing)
{
    const auto value = wiola::library::make_tag_value("  Genre\t", "  Nu Jazz  ");

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->key, "Genre");
    EXPECT_EQ(value->value, "Nu Jazz");
    EXPECT_EQ(wiola::library::normalized_tag_text(value->key), "genre");
    EXPECT_EQ(wiola::library::normalized_tag_text(value->value), "nu jazz");
}

TEST(TagValue, RejectsBlankControlAndOverLimitText)
{
    EXPECT_FALSE(wiola::library::make_tag_value(" \t ", "Jazz").has_value());
    EXPECT_FALSE(wiola::library::make_tag_value("genre", "\n").has_value());
    EXPECT_FALSE(wiola::library::make_tag_value("gen\nre", "Jazz").has_value());
    EXPECT_FALSE(wiola::library::make_tag_value("genre", std::string{"Nu\0Jazz", 7}).has_value());
    EXPECT_FALSE(wiola::library::make_tag_value(
        std::string(wiola::library::max_tag_key_bytes + 1, 'k'), "Jazz"
    )
            .has_value());
    EXPECT_FALSE(wiola::library::make_tag_value("genre",
        std::string(wiola::library::max_tag_value_bytes + 1, 'v'))
            .has_value());

    EXPECT_TRUE(wiola::library::make_tag_value(std::string(wiola::library::max_tag_key_bytes, 'k'),
        std::string(wiola::library::max_tag_value_bytes, 'v'))
            .has_value());
}

} // namespace
