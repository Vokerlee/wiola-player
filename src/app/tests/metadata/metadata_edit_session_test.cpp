/**
 * @file
 * @brief Captured metadata targets, common facts and immutable edit expectations.
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
#include <wiola/app/metadata/metadata_edit_session.hpp>

namespace {

TEST(MetadataEditSession, CapturesOwnedUniqueTargetsInStableIdentityOrder)
{
    using namespace wiola;
    library::TrackRecord first, second, invalid;
    first.id = {"first"};
    first.embedded.title = "First";
    second.id = {"second"};
    second.embedded.title = "Second";
    second.overrides.artist = "Override";
    const auto session =
        app::MetadataEditSession::capture({&second, &first, &first, nullptr, &invalid});
    ASSERT_EQ(session.targets().size(), 2U);
    EXPECT_EQ(session.media(), (std::vector<library::MediaId>{first.id, second.id}));
    EXPECT_TRUE(session.overrides_present());
    first.embedded.title = "Changed outside the captured session";
    EXPECT_EQ(session.targets().front().original.title, "First");
    EXPECT_TRUE(session.valid());
    EXPECT_TRUE(session.mixed(app::StandardMetadataField::title));
}

TEST(MetadataEditSession, CommonAndMixedValuesFollowOnlyTheCapturedRecords)
{
    using namespace wiola;
    library::TrackRecord first, second;
    first.id = {"first"};
    first.embedded.title = "First";
    first.embedded.artist = "Shared artist";
    first.embedded.year = 1990;
    second = first;
    second.id = {"second"};
    second.embedded.title = "Second";
    second.embedded.year = 2000;
    const auto session = app::MetadataEditSession::capture({&first, &second});
    EXPECT_EQ(session.common().artist, first.embedded.artist);
    EXPECT_FALSE(session.mixed(app::StandardMetadataField::artist));
    EXPECT_TRUE(session.mixed(app::StandardMetadataField::title));
    EXPECT_TRUE(session.mixed(app::StandardMetadataField::year));
    EXPECT_TRUE(session.common().title.empty());
    EXPECT_FALSE(session.common().year);
    EXPECT_FALSE(session.overrides_present());
    EXPECT_EQ(session.expectations().at(first.id).metadata_revision, first.metadata_revision);
}

TEST(MetadataEditSession, DraftChangesDoNotReplaceCapturedTargetsOrExpectations)
{
    using namespace wiola;
    library::TrackRecord record;
    record.id = {"media"};
    record.metadata_revision = 4;
    record.content_revision = 2;
    record.fingerprint = "captured";
    auto session = app::MetadataEditSession::capture({&record});
    const auto original = session;
    library::StandardMetadataPatch patch;
    patch.title = "Draft";
    session.set_draft(patch);
    EXPECT_TRUE(session.same_targets(original));
    EXPECT_EQ(session.draft().title, patch.title);
    ++record.metadata_revision;
    EXPECT_FALSE(session.same_targets(app::MetadataEditSession::capture({&record})));
    const auto expected = session.expectations().at(record.id);
    EXPECT_EQ(expected.metadata_revision, original.targets().front().expected.metadata_revision);
    EXPECT_EQ(expected.fingerprint, "captured");
}

TEST(MetadataEditSession, EmptyCaptureHasNoTargetsFactsOrOverrides)
{
    const auto session = wiola::app::MetadataEditSession::capture({nullptr});
    EXPECT_FALSE(session.valid());
    EXPECT_TRUE(session.media().empty());
    EXPECT_TRUE(session.expectations().empty());
    EXPECT_FALSE(session.overrides_present());
    EXPECT_TRUE(wiola::library::metadata_patch_empty(session.draft()));
}

} // namespace
