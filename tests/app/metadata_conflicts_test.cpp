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

#include <app/metadata_conflicts.hpp>

#include <library/fingerprint.hpp>
#include <library/in_memory_repository.hpp>

#include <gtest/gtest.h>

#include <fstream>

namespace {

class MetadataConflicts : public testing::Test {
protected:
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() / "wiola-metadata-conflicts";
        std::filesystem::remove_all(directory);
        ASSERT_TRUE(std::filesystem::create_directories(directory));
        location = directory / "track.wav";
        std::ofstream{location} << "audio";
        expected.fingerprint = *wiola::library::fingerprint_media(location);
        expected.metadata_revision = 4;
        expected.content_revision = 2;

        wiola::library::TrackRecord track;
        track.id = media;
        track.locations = {location};
        track.fingerprint = expected.fingerprint;
        track.metadata_revision = 4;
        track.content_revision = 2;
        ASSERT_TRUE(repository.put_track(track));
    }

    wiola::library::MediaId media{"media"};
    wiola::library::InMemoryRepository repository;
    wiola::app::ExpectedMediaState expected;
    std::filesystem::path location;
    std::filesystem::path directory;

    void TearDown() override { std::filesystem::remove_all(directory); }
};

TEST_F(MetadataConflicts, AcceptsMatchingExpectedState)
{
    wiola::app::MetadataConflictDetector detector;
    EXPECT_EQ(detector.check(repository, media, expected), wiola::app::MetadataConflictKind::none);
}

TEST_F(MetadataConflicts, DetectsMetadataAndContentRevisionChanges)
{
    wiola::app::MetadataConflictDetector detector;
    auto changed = repository.track(media);
    auto updated = *changed;
    ++updated.metadata_revision;
    ASSERT_TRUE(repository.put_track(updated));
    EXPECT_EQ(detector.check(repository, media, expected),
        wiola::app::MetadataConflictKind::metadata_changed);

    updated.metadata_revision = expected.metadata_revision;
    ++updated.content_revision;
    ASSERT_TRUE(repository.put_track(updated));
    EXPECT_EQ(detector.check(repository, media, expected),
        wiola::app::MetadataConflictKind::content_changed);
}

TEST_F(MetadataConflicts, DetectsChangedFilesMissingTracksAndUnreadableLocations)
{
    wiola::app::MetadataConflictDetector detector;
    std::ofstream{location} << "changed";
    EXPECT_EQ(detector.check(repository, media, expected),
        wiola::app::MetadataConflictKind::content_changed);

    EXPECT_EQ(detector.check(repository, wiola::library::MediaId{"missing"}, expected),
        wiola::app::MetadataConflictKind::missing_media);

    std::filesystem::remove(location);
    EXPECT_EQ(detector.check(repository, media, expected),
        wiola::app::MetadataConflictKind::unreadable);
}

} // namespace
