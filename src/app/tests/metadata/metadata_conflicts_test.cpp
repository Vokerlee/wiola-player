/**
 * @file
 * @brief External metadata and file conflict regression tests.
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

#include <wiola/app/metadata/metadata_conflicts.hpp>
#include <wiola/library/in_memory_repository.hpp>

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

namespace {

class MetadataConflicts : public testing::Test {
protected:
    void SetUp() override
    {
        expected = {.metadata_revision = 4, .content_revision = 2, .fingerprint = "captured"};
        observed = expected.fingerprint;

        wiola::library::TrackRecord track;
        track.id = media;
        track.locations = {location};
        track.fingerprint = expected.fingerprint;
        track.metadata_revision = expected.metadata_revision;
        track.content_revision = expected.content_revision;
        ASSERT_TRUE(repository.put_track(track));
    }

    wiola::app::MetadataConflictDetector detector()
    {
        return wiola::app::MetadataConflictDetector{[this](const auto& path) {
            requested.push_back(path);
            return observed;
        }};
    }

    wiola::library::MediaId media{"media"};
    wiola::library::InMemoryRepository repository;
    wiola::app::ExpectedMediaState expected;
    std::filesystem::path location{"recorded-location"};
    std::optional<std::string> observed;
    std::vector<std::filesystem::path> requested;
};

TEST_F(MetadataConflicts, AcceptsMatchingExpectedState)
{
    auto check = detector();

    EXPECT_EQ(check.check(repository, media, expected), wiola::app::MetadataConflictKind::none);
    EXPECT_EQ(requested, (std::vector<std::filesystem::path>{location}));

    const std::filesystem::path supplied{"explicit-location"};

    EXPECT_EQ(check.check(repository, media, expected, supplied),
        wiola::app::MetadataConflictKind::none);
    EXPECT_EQ(requested.back(), supplied);
}

TEST_F(MetadataConflicts, DetectsMetadataAndContentRevisionChangesWithoutReadingTheSource)
{
    auto check = detector();
    auto updated = *repository.track(media);
    ++updated.metadata_revision;
    ASSERT_TRUE(repository.put_track(updated));
    EXPECT_EQ(check.check(repository, media, expected),
        wiola::app::MetadataConflictKind::metadata_changed);

    updated.metadata_revision = expected.metadata_revision;
    ++updated.content_revision;
    ASSERT_TRUE(repository.put_track(updated));
    EXPECT_EQ(check.check(repository, media, expected),
        wiola::app::MetadataConflictKind::content_changed);
    EXPECT_TRUE(requested.empty());
}

TEST_F(MetadataConflicts, DetectsChangedTokensMissingTracksAndUnavailableSources)
{
    auto check = detector();

    observed = "changed";
    EXPECT_EQ(check.check(repository, media, expected),
        wiola::app::MetadataConflictKind::content_changed);
    EXPECT_EQ(check.check(repository, wiola::library::MediaId{"missing"}, expected),
        wiola::app::MetadataConflictKind::missing_media);

    observed.reset();
    EXPECT_EQ(check.check(repository, media, expected),
        wiola::app::MetadataConflictKind::unreadable);
}

TEST_F(MetadataConflicts, RequiresAReaderAndLocationOnlyWhenAFingerprintWasCaptured)
{
    wiola::app::MetadataConflictDetector unavailable;

    EXPECT_EQ(unavailable.check(repository, media, expected),
        wiola::app::MetadataConflictKind::unreadable);

    auto check = detector();
    auto updated = *repository.track(media);
    updated.locations.clear();
    ASSERT_TRUE(repository.put_track(updated));
    EXPECT_EQ(check.check(repository, media, expected),
        wiola::app::MetadataConflictKind::unreadable);

    expected.fingerprint.clear();
    EXPECT_EQ(check.check(repository, media, expected), wiola::app::MetadataConflictKind::none);
    EXPECT_TRUE(requested.empty());
}

TEST_F(MetadataConflicts, DriverExceptionsAreReportedAsUnreadable)
{
    wiola::app::MetadataConflictDetector check{[](const auto&) -> std::optional<std::string> {
        throw std::runtime_error{"reader failed"};
    }};
    EXPECT_EQ(check.check(repository, media, expected),
        wiola::app::MetadataConflictKind::unreadable);
}

} // namespace
