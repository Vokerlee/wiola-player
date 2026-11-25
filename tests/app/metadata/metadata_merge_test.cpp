/**
 * @file
 * @brief Background metadata reading and merge regression tests.
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

#include <app/metadata/background_metadata.hpp>
#include <app/metadata/metadata_merge.hpp>

#include <gtest/gtest.h>

#include <condition_variable>
#include <mutex>

namespace {

class FakeAsset final : public wiola::media::MediaAsset {
public:
    wiola::media::Result<wiola::media::MediaInfo>
    inspect(const wiola::media::OperationContext&) override
    {
        return wiola::media::unexpected{
            wiola::media::Error{wiola::media::ErrorCategory::unsupported, "test"}
        };
    }

    wiola::media::Result<wiola::media::Metadata>
    read_metadata(const wiola::media::OperationContext&) override
    {
        wiola::media::Metadata metadata;
        metadata.track.title = "Learned title";
        metadata.track.album = "Learned album";
        metadata.track.duration = wiola::units::Time{42};

        return metadata;
    }

    wiola::media::Result<std::unique_ptr<wiola::media::AudioDecoder>>
    create_decoder(const wiola::media::OperationContext&) override
    {
        return wiola::media::unexpected{
            wiola::media::Error{wiola::media::ErrorCategory::unsupported, "test"}
        };
    }
};

class FakeProvider final : public wiola::media::MediaProvider {
public:
    wiola::media::Result<std::unique_ptr<wiola::media::MediaAsset>>
    open(const wiola::media::ResourceReference&, const wiola::media::OperationContext&) override
    {
        return std::unique_ptr<wiola::media::MediaAsset>{std::make_unique<FakeAsset>()};
    }

    std::vector<std::string> playback_extensions() const override { return {".wav"}; }
};

TEST(MetadataMerge, LearnsOnlyChangedFieldsWithoutTouchingUserLayers)
{
    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord record;
    record.id = wiola::library::MediaId{"media"};
    record.embedded.title = "Old title";
    record.embedded.artist = "Known artist";
    record.embedded.album = "Old album";
    record.overrides.title = "User title";
    record.metadata_revision = 7;
    record.tags = {
        {.id = wiola::library::TagId{"tag"}, .key = "genre", .value = "Jazz"}
    };
    ASSERT_TRUE(repository.put_track(record));

    wiola::media::Metadata learned;
    learned.track.title = "Learned title";
    learned.track.album = "Learned album";
    learned.track.duration = wiola::units::Time{42};
    learned.track.genre = "Jazz";
    learned.track.year = 2024;
    learned.track.comment = "Live";
    learned.track.composer = "Composer";
    learned.track.track_number = 3;
    learned.track.disc_number = 2;

    wiola::app::MetadataMerge merge;
    ASSERT_TRUE(merge.merge(repository, record.id, learned));

    const auto* updated = repository.track(record.id);
    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(updated->embedded.title, "Learned title");
    EXPECT_EQ(updated->embedded.artist, "Known artist");
    EXPECT_EQ(updated->embedded.album, "Learned album");
    EXPECT_EQ(updated->embedded.duration, wiola::units::Time{42});
    EXPECT_EQ(updated->embedded.genre, "Jazz");
    EXPECT_EQ(updated->embedded.year, 2024);
    EXPECT_EQ(updated->embedded.comment, "Live");
    EXPECT_EQ(updated->embedded.composer, "Composer");
    EXPECT_EQ(updated->embedded.track_number, 3);
    EXPECT_EQ(updated->embedded.disc_number, 2);
    EXPECT_EQ(updated->display_title(), "User title");
    EXPECT_EQ(updated->tags.size(), 1);
    EXPECT_EQ(updated->metadata_revision, 8);

    EXPECT_FALSE(merge.merge(repository, record.id, learned));
    EXPECT_EQ(repository.track(record.id)->metadata_revision, 8);
    EXPECT_FALSE(merge.merge(repository, wiola::library::MediaId{"missing"}, learned));
}

TEST(MetadataMerge, ReadsAndMergesMetadataThroughTheBackgroundWorker)
{
    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord record;
    record.id = wiola::library::MediaId{"media"};
    ASSERT_TRUE(repository.put_track(record));

    wiola::app::BackgroundMetadata reader{std::make_shared<FakeProvider>()};
    std::mutex mutex;
    std::condition_variable received;
    std::optional<wiola::app::BackgroundMetadataResult> result;

    ASSERT_TRUE(reader.enqueue(record.id, "track.wav",
        [&](wiola::app::BackgroundMetadataResult incoming) {
            std::lock_guard lock{mutex};
            result = std::move(incoming);
            received.notify_one();
        }));

    {
        std::unique_lock lock{mutex};
        ASSERT_TRUE(received.wait_for(lock, std::chrono::seconds{5},
            [&] { return result.has_value(); }));
    }

    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->error.has_value());
    ASSERT_TRUE(result->metadata.has_value());

    wiola::app::MetadataMerge merge;
    EXPECT_TRUE(merge.merge(repository, record.id, *result->metadata));
    EXPECT_EQ(repository.track(record.id)->embedded.title, "Learned title");
    EXPECT_EQ(repository.track(record.id)->embedded.album, "Learned album");
}

} // namespace
