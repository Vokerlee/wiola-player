/**
 * @file
 * @brief Metadata writer contract regression tests.
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

#include <wiola/media/provider.hpp>

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <utility>

namespace {

class ReadOnlyAsset final : public wiola::media::MediaAsset {
public:
    wiola::media::Result<wiola::media::MediaInfo>
    inspect(const wiola::media::OperationContext&) override
    {
        return wiola::media::MediaInfo{};
    }

    wiola::media::Result<wiola::media::Metadata>
    read_metadata(const wiola::media::OperationContext&) override
    {
        return wiola::media::Metadata{};
    }

    wiola::media::Result<std::unique_ptr<wiola::media::AudioDecoder>>
    create_decoder(const wiola::media::OperationContext&) override
    {
        return std::unique_ptr<wiola::media::AudioDecoder>{};
    }
};

TEST(MetadataWriter, RepresentsFieldEditsAndExpectedStateWithoutLibraryDependencies)
{
    wiola::media::MetadataWriteRequest request{
        .overrides = {.title = "Title",
                      .artist = std::string{},
                      .album = {},
                      .genre = {},
                      .year = 2026,
                      .comment = {},
                      .composer = {},
                      .track_number = {},
                      .disc_number = {}},
        .expected = {.metadata_revision = 4, .content_revision = 9, .fingerprint = "abc"}
    };

    EXPECT_FALSE(request.overrides.empty());
    EXPECT_TRUE(request.overrides.artist->empty());
    EXPECT_EQ(request.expected.metadata_revision, 4);
    EXPECT_EQ(request.expected.content_revision, 9);
    EXPECT_EQ(request.expected.fingerprint, "abc");
}

TEST(MetadataWriter, ReadOnlyAssetsRejectWritesWithoutModifyingTheSource)
{
    ReadOnlyAsset asset;
    const wiola::media::MetadataWriteOutcome outcome = asset.write_metadata({});

    EXPECT_EQ(outcome.state, wiola::media::MetadataWriteState::failed);
    EXPECT_EQ(outcome.error, wiola::media::MetadataWriteError::unsupported);
    EXPECT_FALSE(outcome.source_modified);
    EXPECT_FALSE(outcome.detail.empty());
    EXPECT_TRUE(outcome.staged_output.empty());
    EXPECT_FALSE(asset.metadata_write_capabilities().any());
}

TEST(MetadataWriter, EveryWriteCapabilityIndependentlyEnablesWriting)
{
    using wiola::media::MetadataWriteCapabilities;

    EXPECT_FALSE(MetadataWriteCapabilities{}.any());

    for (const auto& [name, member] : std::array{
             std::pair{"title",        &MetadataWriteCapabilities::title       },
             std::pair{"artist",       &MetadataWriteCapabilities::artist      },
             std::pair{"album",        &MetadataWriteCapabilities::album       },
             std::pair{"genre",        &MetadataWriteCapabilities::genre       },
             std::pair{"year",         &MetadataWriteCapabilities::year        },
             std::pair{"comment",      &MetadataWriteCapabilities::comment     },
             std::pair{"composer",     &MetadataWriteCapabilities::composer    },
             std::pair{"track number", &MetadataWriteCapabilities::track_number},
             std::pair{"disc number",  &MetadataWriteCapabilities::disc_number }
    }) {
        SCOPED_TRACE(name);
        MetadataWriteCapabilities capabilities;
        capabilities.*member = true;

        EXPECT_TRUE(capabilities.any());

        capabilities.*member = false;

        EXPECT_FALSE(capabilities.any());
    }
}

TEST(MetadataWriter, EveryTextOverrideDistinguishesClearingAFieldFromLeavingItAlone)
{
    using wiola::media::MetadataOverrides;

    EXPECT_TRUE(MetadataOverrides{}.empty());

    for (const auto& [name, member] : std::array{
             std::pair{"title",    &MetadataOverrides::title   },
             std::pair{"artist",   &MetadataOverrides::artist  },
             std::pair{"album",    &MetadataOverrides::album   },
             std::pair{"genre",    &MetadataOverrides::genre   },
             std::pair{"comment",  &MetadataOverrides::comment },
             std::pair{"composer", &MetadataOverrides::composer}
    }) {
        SCOPED_TRACE(name);
        MetadataOverrides overrides;
        overrides.*member = std::string{};

        EXPECT_FALSE(overrides.empty());
        ASSERT_TRUE((overrides.*member).has_value());
        EXPECT_TRUE((overrides.*member)->empty());

        (overrides.*member).reset();

        EXPECT_TRUE(overrides.empty());
    }
}

TEST(MetadataWriter, EveryNumericOverrideDistinguishesZeroFromNoEdit)
{
    using wiola::media::MetadataOverrides;

    for (const auto& [name, member] : std::array{
             std::pair{"year",         &MetadataOverrides::year        },
             std::pair{"track number", &MetadataOverrides::track_number},
             std::pair{"disc number",  &MetadataOverrides::disc_number }
    }) {
        SCOPED_TRACE(name);
        MetadataOverrides overrides;
        overrides.*member = 0;

        EXPECT_FALSE(overrides.empty());

        (overrides.*member).reset();

        EXPECT_TRUE(overrides.empty());
    }
}

} // namespace
