/**
 * @file
 * @brief Content identity and relocation reconciliation tests.
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

#include <library/fingerprint.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace wiola::library;

struct TemporaryDirectory {
    std::filesystem::path path{std::filesystem::temp_directory_path() /
        ("wiola-fingerprint-" + std::to_string(std::random_device{}()))};

    TemporaryDirectory() { std::filesystem::create_directories(path); }

    ~TemporaryDirectory() { std::filesystem::remove_all(path); }
};

void write(const std::filesystem::path& file, const std::string& content)
{
    std::ofstream{file, std::ios::binary} << content;
}

TEST(MediaFingerprint, DependsOnContentRatherThanLocation)
{
    const TemporaryDirectory directory;
    const auto first = directory.path / "first.wav";
    const auto second = directory.path / "second.wav";

    write(first, "same content");
    write(second, "same content");

    ASSERT_TRUE(fingerprint_media(first).has_value());
    EXPECT_EQ(fingerprint_media(first), fingerprint_media(second));

    write(second, "different");

    EXPECT_NE(fingerprint_media(first), fingerprint_media(second));
}

TEST(MediaFingerprint, MatchesOnlyOneUnambiguousStoredRecord)
{
    const TemporaryDirectory directory;
    const auto candidate = directory.path / "candidate.wav";
    write(candidate, "content");

    TrackRecord first;
    first.id = {.value = "media-1"};
    first.fingerprint = *fingerprint_media(candidate);

    TrackRecord second;
    second.id = {.value = "media-2"};
    second.fingerprint = *fingerprint_media(candidate);
    const std::vector<TrackRecord> tracks{first, second};

    EXPECT_FALSE(match_relocation(tracks, candidate).has_value());

    second.fingerprint = "different";
    const std::vector<TrackRecord> distinct{first, second};
    const auto match = match_relocation(distinct, candidate);

    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(*match, first.id);
}

TEST(MediaFingerprint, ReconcilesMovesAndEmbeddedEditsWithoutLosingUserMetadata)
{
    const TemporaryDirectory directory;
    const auto original = directory.path / "original.wav";
    const auto moved = directory.path / "moved.wav";
    write(original, "original");
    write(moved, "original");

    TrackRecord track;
    track.id = {.value = "media-1"};
    track.fingerprint = *fingerprint_media(original);
    track.locations = {original};
    track.overrides.title = "My title";
    track.tags = {
        {.id = {.value = "tag-1"}, .key = "genre", .value = "Jazz"}
    };

    const auto move = reconcile_location(track, moved);
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(move->kind, RelocationKind::unchanged);
    EXPECT_EQ(track.content_revision, 0U);
    EXPECT_EQ(track.locations.size(), 2U);

    write(moved, "embedded metadata changed");

    const auto edit = reconcile_location(track, moved);
    ASSERT_TRUE(edit.has_value());
    EXPECT_EQ(edit->kind, RelocationKind::edited);
    EXPECT_EQ(track.content_revision, 1U);
    EXPECT_EQ(track.overrides.title, "My title");
    ASSERT_EQ(track.tags.size(), 1U);
    EXPECT_EQ(track.tags.front().value, "Jazz");
    EXPECT_FALSE(track.missing);
}

TEST(MediaFingerprint, LearnsAnInitialFingerprintWithoutCallingItAnEdit)
{
    const TemporaryDirectory directory;
    const auto file = directory.path / "track.wav";
    write(file, "content");

    TrackRecord track;
    track.id = {.value = "media-1"};

    const auto learned = reconcile_location(track, file);

    ASSERT_TRUE(learned.has_value());
    EXPECT_EQ(learned->kind, RelocationKind::learned);
    EXPECT_EQ(track.content_revision, 0U);
    EXPECT_FALSE(track.fingerprint.empty());
}

TEST(MediaFingerprint, UnreadableLocationLeavesTheRecordUnchanged)
{
    TrackRecord track;
    track.id = {.value = "media-1"};
    track.fingerprint = "known";
    track.missing = true;

    EXPECT_FALSE(reconcile_location(track, "/definitely/missing/track.wav").has_value());
    EXPECT_EQ(track.fingerprint, "known");
    EXPECT_TRUE(track.missing);
    EXPECT_TRUE(track.locations.empty());
}

} // namespace
