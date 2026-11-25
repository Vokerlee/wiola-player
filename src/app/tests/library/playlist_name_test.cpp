/**
 * @file
 * @brief Portable playlist-name normalization and boundary tests.
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
#include <string>
#include <string_view>
#include <wiola/app/library/playlist_name.hpp>

namespace {

using wiola::app::PlaylistName;

TEST(PlaylistName, TrimsUnicodeWhitespaceAndPreservesInteriorText)
{
    for (const std::string_view space :
        {"\t", "\n", "\v", "\f", "\r", " ", "\xc2\x85", "\xc2\xa0", "\xe1\x9a\x80", "\xe2\x80\x80",
            "\xe2\x80\x81", "\xe2\x80\x82", "\xe2\x80\x83", "\xe2\x80\x84", "\xe2\x80\x85",
            "\xe2\x80\x86", "\xe2\x80\x87", "\xe2\x80\x88", "\xe2\x80\x89", "\xe2\x80\x8a",
            "\xe2\x80\xa8", "\xe2\x80\xa9", "\xe2\x80\xaf", "\xe2\x81\x9f", "\xe3\x80\x80"}) {
        const std::string text = std::string{space} + "Music\t\u97f3\u697d" + std::string{space};
        const auto name = PlaylistName::parse(text);
        ASSERT_TRUE(name);
        EXPECT_EQ(name->value(), "Music\t\u97f3\u697d");
        EXPECT_FALSE(PlaylistName::parse(space));
    }
    EXPECT_FALSE(PlaylistName::parse({}));
}

TEST(PlaylistName, AppliesTheBudgetAfterTrimming)
{
    const std::string exact(PlaylistName::max_utf16_units, 'x');
    const auto name = PlaylistName::parse("  " + exact + "  ");
    ASSERT_TRUE(name);
    EXPECT_EQ(name->value(), exact);
    EXPECT_FALSE(PlaylistName::parse(exact + 'x'));
    EXPECT_FALSE(PlaylistName::parse("x" + std::string(PlaylistName::max_utf16_units, ' ') + 'y'));
    const auto trailing =
        PlaylistName::parse(exact + std::string(PlaylistName::max_utf16_units, ' '));
    ASSERT_TRUE(trailing);
    EXPECT_EQ(trailing->value(), exact);
}

TEST(PlaylistName, CountsSupplementaryScalarsAsTwoUtf16Units)
{
    std::string exact;
    for (std::size_t i = 0; i != PlaylistName::max_utf16_units / 2; ++i)
        exact += "\U0001f3b5";
    const auto name = PlaylistName::parse(exact);
    ASSERT_TRUE(name);
    EXPECT_EQ(name->value(), exact);
    EXPECT_FALSE(PlaylistName::parse(exact + 'x'));
    EXPECT_FALSE(PlaylistName::parse(exact + "\U0001f3b5"));
}

TEST(PlaylistName, ConsumesOnlyOneInitialUtf8Bom)
{
    const std::string bom = "\xef\xbb\xbf";
    const auto initial = PlaylistName::parse(bom + "  Music  ");
    ASSERT_TRUE(initial);
    EXPECT_EQ(initial->value(), "Music");
    EXPECT_FALSE(PlaylistName::parse(bom));
    for (const auto& text : {bom + bom, " " + bom, "A" + bom}) {
        const auto name = PlaylistName::parse(text);
        ASSERT_TRUE(name);
        EXPECT_EQ(name->value(), text == "A" + bom ? text : bom);
    }
}

TEST(PlaylistName, RepairsEachMalformedByteWithoutAcceptingNonScalarUtf8)
{
    const std::string replacement = "\xef\xbf\xbd";

    struct Example {
        std::string input;
        std::string expected;
    };

    for (const auto& example : {
             Example{"\x80",                   replacement                                          },
             Example{"\xff",                   replacement                                          },
             Example{"\xc0\xaf",               replacement + replacement                            },
             Example{"\xe2\x82",               replacement + replacement                            },
             Example{"\xf0\x9f\x92",           replacement + replacement + replacement              },
             Example{"\xe0\x80\xaf",           replacement + replacement + replacement              },
             Example{"\xed\xa0\x80",           replacement + replacement + replacement              },
             Example{"\xed\xbf\xbf",           replacement + replacement + replacement              },
             Example{"\xf4\x90\x80\x80",       replacement + replacement + replacement + replacement},
             Example{"\xe2(\xa1",              replacement + '(' + replacement                      },
             Example{"\xe2\xff\x80",           replacement + replacement + replacement              },
             Example{"\xf0\x90\x80"
                     "A", replacement + replacement + replacement + 'A'        }
    }) {
        const auto name = PlaylistName::parse(example.input);
        ASSERT_TRUE(name);
        EXPECT_EQ(name->value(), example.expected);
    }
}

TEST(PlaylistName, PreservesScalarBoundariesAndInteriorNul)
{
    for (const std::string_view text : {"\xc2\x80", "\xdf\xbf", "\xe0\xa0\x80", "\xed\x9f\xbf",
             "\xee\x80\x80", "\xf0\x90\x80\x80", "\xf4\x8f\xbf\xbf"}) {
        const auto name = PlaylistName::parse(text);
        ASSERT_TRUE(name);
        EXPECT_EQ(name->value(), text);
    }
    const auto text = std::string{"A"} + '\0' + 'B';
    const auto name = PlaylistName::parse(" " + text + " ");
    ASSERT_TRUE(name);
    EXPECT_EQ(name->value(), text);
}

TEST(PlaylistName, AppliesTheSameBudgetToRepairedInput)
{
    const std::string exact(PlaylistName::max_utf16_units, static_cast<char>(0xff));
    const auto name = PlaylistName::parse(exact);
    ASSERT_TRUE(name);
    EXPECT_EQ(name->value().size(), PlaylistName::max_utf16_units * 3);
    EXPECT_FALSE(PlaylistName::parse(exact + static_cast<char>(0xff)));
}

} // namespace
