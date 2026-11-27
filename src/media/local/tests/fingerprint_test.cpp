/**
 * @file
 * @brief Native content fingerprint compatibility and cancellation tests.
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

#include <fixtures/temporary_directory.hpp>
#include <wiola/media/local/fingerprint.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <locale>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

namespace {

using wiola::media::fingerprint_media;
using wiola::media::fingerprint_media_cancellable;
using wiola::testing::TemporaryDirectory;

void write(const std::filesystem::path& file, std::string_view content)
{
    std::ofstream output{file, std::ios::binary};
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    ASSERT_TRUE(output);
}

class GroupedNumbers final : public std::numpunct<char> {
    char do_thousands_sep() const override { return '_'; }

    std::string do_grouping() const override { return "\1"; }
};

class ScopedLocale final {
public:
    explicit ScopedLocale(std::locale locale)
        : previous_{std::locale::global(std::move(locale))}
    {
    }

    ScopedLocale(const ScopedLocale&) = delete;
    ScopedLocale& operator=(const ScopedLocale&) = delete;

    ~ScopedLocale() { std::locale::global(previous_); }

private:
    std::locale previous_;
};

TEST(MediaFingerprint, StoredIdentityDoesNotDependOnTheProcessNumericLocale)
{
    const TemporaryDirectory directory;
    const auto file = directory.path() / "track.wav";
    write(file, "foobar");

    const ScopedLocale grouped{
        std::locale{std::locale::classic(), new GroupedNumbers}
    };

    EXPECT_EQ(fingerprint_media(file), "fnv1a64:85944171f73967e8:0000000000000006");
}

TEST(MediaFingerprint, DependsOnContentRatherThanLocation)
{
    const TemporaryDirectory directory;
    const auto first = directory.path() / "first.wav";
    const auto second = directory.path() / std::filesystem::path{u8"музыка.wav"};

    write(first, "same content");
    write(second, "same content");

    ASSERT_TRUE(fingerprint_media(first));
    EXPECT_EQ(fingerprint_media(first), fingerprint_media(second));

    write(second, "different");
    EXPECT_NE(fingerprint_media(first), fingerprint_media(second));
}

TEST(MediaFingerprint, PreservesTheStoredDigestSpellingForKnownContent)
{
    const TemporaryDirectory directory;
    const auto file = directory.path() / "track.wav";
    write(file, "");
    EXPECT_EQ(fingerprint_media(file), "fnv1a64:cbf29ce484222325:0000000000000000");

    write(file, "a");
    EXPECT_EQ(fingerprint_media(file), "fnv1a64:af63dc4c8601ec8c:0000000000000001");

    write(file, "foobar");
    EXPECT_EQ(fingerprint_media(file), "fnv1a64:85944171f73967e8:0000000000000006");
}

TEST(MediaFingerprint, ReadsBeyondTheFirstBlockIncludingEmbeddedNulls)
{
    const TemporaryDirectory directory;
    const auto file = directory.path() / "track.wav";
    std::string content(128 * 1024, '\0');
    write(file, content);

    const auto original = fingerprint_media(file);

    ASSERT_TRUE(original);

    content.back() = 'x';
    write(file, content);

    const auto edited = fingerprint_media(file);

    ASSERT_TRUE(edited);
    EXPECT_NE(edited, original);
    EXPECT_TRUE(edited->ends_with(":0000000000020000"));
}

TEST(MediaFingerprint, MissingAndUnreadableInputsProduceNoIdentity)
{
    const TemporaryDirectory directory;

    EXPECT_FALSE(fingerprint_media(directory.path() / "missing.wav"));
    EXPECT_FALSE(fingerprint_media(directory.path()));
}

TEST(MediaFingerprint, PreCancelledReadNeverPublishesAContentIdentity)
{
    const TemporaryDirectory directory;
    const auto file = directory.path() / "cancelled.wav";
    write(file, "readable content");

    std::stop_source cancellation;

    ASSERT_TRUE(cancellation.request_stop());

    EXPECT_FALSE(fingerprint_media_cancellable(file, cancellation.get_token()));
    EXPECT_TRUE(fingerprint_media(file));
}

} // namespace
