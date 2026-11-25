/**
 * @file
 * @brief Owned resource identity, display hints and provider separation contracts.
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

#include <gtest/gtest.h>
#include <wiola/media/resource.hpp>

#include <map>
#include <string>
#include <type_traits>

namespace {

using wiola::media::ResourceReference;

static_assert(std::is_nothrow_move_constructible_v<ResourceReference>);
static_assert(std::is_nothrow_move_assignable_v<ResourceReference>);

TEST(ResourceReference, OwnsItsIdentityAndLabelWithoutInterpretingTheKey)
{
    std::string source{"supplied driver"};
    std::string key{"opaque/key?x=1"};
    std::string label{"Human title"};
    const ResourceReference value{source, key, label};
    source.clear();
    key.clear();
    label.clear();
    EXPECT_EQ(value.source(), "supplied driver");
    EXPECT_EQ(value.key(), "opaque/key?x=1");
    EXPECT_EQ(value.label(), "Human title");
    EXPECT_FALSE(value.empty());
    EXPECT_TRUE(ResourceReference{}.empty());
    EXPECT_FALSE((ResourceReference{"driver", "", ""}.empty()));
    EXPECT_FALSE((ResourceReference{"", "key", ""}.empty()));
}

TEST(ResourceReference, LabelsDoNotChangeIdentityOrMetadataGrouping)
{
    const ResourceReference first{"driver", "object", "First occurrence"};
    const ResourceReference second{"driver", "object", "Second occurrence"};
    EXPECT_EQ(first, second);
    EXPECT_EQ(first <=> second, std::strong_ordering::equal);
    std::map<ResourceReference, int> resources;
    resources[first] = 1;
    resources[second] += 1;
    ASSERT_EQ(resources.size(), 1U);
    EXPECT_EQ(resources.begin()->second, 2);
    EXPECT_EQ(second.label(), "Second occurrence");
}

TEST(ResourceReference, SourceNamespacesKeepEqualKeysIndependent)
{
    const ResourceReference a{"a", "key", "Shared label"};
    const ResourceReference b{"b", "key", "Shared label"};
    const ResourceReference c{"a", "other", "Shared label"};
    EXPECT_NE(a, b);
    EXPECT_NE(a, c);
    EXPECT_LT(a, b);
    EXPECT_GT(b, a);
    EXPECT_LT(a, c);
}

} // namespace
