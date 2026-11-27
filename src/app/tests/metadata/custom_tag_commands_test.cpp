/**
 * @file
 * @brief Transactional custom tag command regression tests.
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

#include <app/metadata/custom_tag_commands.hpp>
#include <wiola/library/tags.hpp>

#include <gtest/gtest.h>

namespace {

class CustomTagCommands : public testing::Test {
protected:
    void SetUp() override
    {
        wiola::library::TrackRecord track;
        track.id = media;
        ASSERT_TRUE(repository.put_track(track));
    }

    wiola::library::MediaId media{"media"};
    wiola::library::InMemoryRepository repository;
    int saves{0};
};

TEST_F(CustomTagCommands, AddsEditsAndRemovesValuesWithStableIdentity)
{
    wiola::app::CustomTagCommands commands{repository,
        [this](const wiola::library::LibraryRepository&) {
            ++saves;
            return true;
        }};

    const auto added = commands.add(media, " Genre ", " Jazz ");

    ASSERT_TRUE(added.succeeded());
    EXPECT_TRUE(added.changed);
    ASSERT_TRUE(added.tag.valid());
    EXPECT_EQ(repository.track(media)->tags.front().key, "Genre");
    EXPECT_EQ(repository.track(media)->tags.front().value, "Jazz");
    EXPECT_EQ(repository.track(media)->metadata_revision, 1);

    const auto edited = commands.edit(media, added.tag, "GENRE", "Nu Jazz");

    ASSERT_TRUE(edited.succeeded());
    EXPECT_TRUE(edited.changed);
    EXPECT_EQ(edited.tag, added.tag);
    EXPECT_EQ(repository.track(media)->tags.front().id, added.tag);
    EXPECT_EQ(repository.track(media)->tags.front().key, "GENRE");
    EXPECT_EQ(repository.track(media)->tags.front().value, "Nu Jazz");
    EXPECT_EQ(repository.track(media)->metadata_revision, 2);

    const auto removed = commands.remove(media, added.tag);

    ASSERT_TRUE(removed.succeeded());
    EXPECT_TRUE(removed.changed);
    EXPECT_TRUE(repository.track(media)->tags.empty());
    EXPECT_EQ(repository.track(media)->metadata_revision, 3);
    EXPECT_EQ(saves, 3);
}

TEST_F(CustomTagCommands, RejectsInvalidDuplicatesMissingRecordsAndOverflow)
{
    wiola::app::CustomTagCommands commands{repository,
        [this](const wiola::library::LibraryRepository&) {
            ++saves;
            return true;
        }};

    const auto first = commands.add(media, "Genre", "Jazz");

    ASSERT_TRUE(first.succeeded());

    EXPECT_EQ(commands.add(media, "genre", "jazz").error,
        wiola::app::CustomTagError::duplicate_value);
    EXPECT_EQ(commands.add(media, " ", "Jazz").error, wiola::app::CustomTagError::invalid_value);
    EXPECT_EQ(commands.add(wiola::library::MediaId{"missing"}, "Genre", "Jazz").error,
        wiola::app::CustomTagError::missing_media);
    EXPECT_EQ(commands.edit(media, wiola::library::TagId{"missing"}, "Genre", "Jazz").error,
        wiola::app::CustomTagError::missing_tag);
    EXPECT_EQ(commands.remove(media, wiola::library::TagId{"missing"}).error,
        wiola::app::CustomTagError::missing_tag);

    const auto noop = commands.edit(media, first.tag, "Genre", "Jazz");

    EXPECT_TRUE(noop.succeeded());
    EXPECT_FALSE(noop.changed);
    EXPECT_EQ(saves, 1);

    for (std::size_t index = 1; index < wiola::library::max_tags_per_track; ++index)
        ASSERT_TRUE(repository.add_tag(media, "index", std::to_string(index)).valid());

    EXPECT_EQ(commands.add(media, "overflow", "value").error,
        wiola::app::CustomTagError::limit_exceeded);
    EXPECT_EQ(saves, 1);
}

TEST_F(CustomTagCommands, RollsBackEveryMutationWhenPersistenceFails)
{
    wiola::app::CustomTagCommands commands{repository,
        [](const wiola::library::LibraryRepository&) { return false; }};

    EXPECT_EQ(commands.add(media, "Genre", "Jazz").error,
        wiola::app::CustomTagError::persistence_failed);
    EXPECT_TRUE(repository.track(media)->tags.empty());
    EXPECT_EQ(repository.track(media)->metadata_revision, 0);

    const wiola::library::TagId retained = repository.add_tag(media, "Genre", "Jazz");

    ASSERT_TRUE(retained.valid());

    EXPECT_EQ(commands.edit(media, retained, "Genre", "Fusion").error,
        wiola::app::CustomTagError::persistence_failed);
    ASSERT_EQ(repository.track(media)->tags.size(), 1);
    EXPECT_EQ(repository.track(media)->tags.front().id, retained);
    EXPECT_EQ(repository.track(media)->tags.front().value, "Jazz");
    EXPECT_EQ(repository.track(media)->metadata_revision, 1);

    EXPECT_EQ(commands.remove(media, retained).error,
        wiola::app::CustomTagError::persistence_failed);
    ASSERT_EQ(repository.track(media)->tags.size(), 1);
    EXPECT_EQ(repository.track(media)->tags.front().value, "Jazz");
    EXPECT_EQ(repository.track(media)->metadata_revision, 1);
}

} // namespace
