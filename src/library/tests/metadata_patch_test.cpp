/**
 * @file
 * @brief Typed metadata patch semantics and file conversion tests.
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

#include <wiola/library/metadata.hpp>
#include <wiola/library/metadata_patch.hpp>

#include <gtest/gtest.h>

namespace {

using namespace wiola::library;

TEST(MetadataPatch, KeepsSetClearAndInheritDistinctForText)
{
    TrackRecord track;
    track.embedded.title = "Embedded";
    track.embedded.artist = "Artist";
    track.overrides.artist = "Retained";

    StandardMetadataPatch patch;

    EXPECT_TRUE(metadata_patch_empty(patch));

    patch.title = "Application";
    ASSERT_EQ(apply_metadata_patch(track.overrides, patch), MetadataPatchResult::changed);
    EXPECT_EQ(resolve_metadata(track).title, "Application");
    EXPECT_EQ(resolve_metadata(track).artist, "Retained");

    patch.title = FieldEdit<std::string>::clear();
    ASSERT_EQ(apply_metadata_patch(track.overrides, patch), MetadataPatchResult::changed);
    EXPECT_TRUE(track.overrides.title.has_value());
    EXPECT_TRUE(resolve_metadata(track).title.empty());

    patch.title = FieldEdit<std::string>::inherit();
    ASSERT_EQ(apply_metadata_patch(track.overrides, patch), MetadataPatchResult::changed);
    EXPECT_FALSE(track.overrides.title.has_value());
    EXPECT_EQ(resolve_metadata(track).title, "Embedded");
    EXPECT_EQ(apply_metadata_patch(track.overrides, patch), MetadataPatchResult::unchanged);
}

TEST(MetadataPatch, NumericZeroClearAndInheritHaveDifferentMeanings)
{
    TrackRecord track;
    track.embedded.year = 2020;

    StandardMetadataPatch patch;
    patch.year = 0;
    ASSERT_EQ(apply_metadata_patch(track.overrides, patch), MetadataPatchResult::changed);
    EXPECT_EQ(resolve_metadata(track).year, 0);

    patch.year = FieldEdit<std::uint32_t>::clear();
    ASSERT_EQ(apply_metadata_patch(track.overrides, patch), MetadataPatchResult::changed);
    ASSERT_TRUE(track.overrides.year.has_value());
    EXPECT_FALSE(track.overrides.year->has_value());
    EXPECT_FALSE(resolve_metadata(track).year.has_value());

    patch.year = FieldEdit<std::uint32_t>::inherit();
    ASSERT_EQ(apply_metadata_patch(track.overrides, patch), MetadataPatchResult::changed);
    EXPECT_FALSE(track.overrides.year.has_value());
    EXPECT_EQ(resolve_metadata(track).year, 2020);
}

TEST(MetadataPatch, InvalidTextRejectsTheWholePatchBeforeChangingAnyField)
{
    StandardFieldOverrides stored;
    stored.title = "Retained";

    const auto previous = stored;
    StandardMetadataPatch patch;
    patch.title = "Must not apply";
    patch.comment = std::string(max_metadata_text_bytes + 1, 'x');
    EXPECT_FALSE(valid_metadata_patch(patch));
    EXPECT_EQ(apply_metadata_patch(stored, patch), MetadataPatchResult::invalid);
    EXPECT_EQ(stored, previous);

    patch.comment = std::string{"bad\0data", 8};
    EXPECT_FALSE(valid_metadata_patch(patch));
    EXPECT_EQ(apply_metadata_patch(stored, patch), MetadataPatchResult::invalid);
    EXPECT_EQ(stored, previous);
    EXPECT_FALSE(file_metadata_patch(patch));
}

TEST(MetadataPatch, StandardTextBudgetCountsEncodedBytesAndPreservesBoundaryContent)
{
    StandardMetadataPatch patch;
    const std::string glyph{"\xe9\x9f\xb3"};
    std::string text;

    for (std::size_t index = 0; index < max_metadata_text_bytes / glyph.size(); ++index)
        text += glyph;

    text.append(max_metadata_text_bytes % glyph.size(), 'a');
    ASSERT_EQ(text.size(), max_metadata_text_bytes);

    patch.comment = text;
    EXPECT_TRUE(valid_metadata_patch(patch));

    StandardFieldOverrides stored;

    ASSERT_EQ(apply_metadata_patch(stored, patch), MetadataPatchResult::changed);
    ASSERT_TRUE(stored.comment);
    EXPECT_EQ(*stored.comment, text);

    const auto file = file_metadata_patch(patch);

    ASSERT_TRUE(file);
    EXPECT_EQ(file->comment, text);

    patch.comment = text + "x";
    EXPECT_FALSE(valid_metadata_patch(patch));
    EXPECT_EQ(apply_metadata_patch(stored, patch), MetadataPatchResult::invalid);
    EXPECT_EQ(*stored.comment, text);
    EXPECT_FALSE(file_metadata_patch(patch));
}

TEST(MetadataPatch, FileConversionDoesNotTurnInheritanceIntoEmbeddedDeletion)
{
    StandardMetadataPatch patch;
    patch.title = FieldEdit<std::string>::inherit();
    patch.artist = "Written";
    patch.comment = FieldEdit<std::string>::clear();
    patch.year = 0;

    auto file = file_metadata_patch(patch);

    ASSERT_TRUE(file);
    EXPECT_FALSE(file->title.has_value());
    EXPECT_EQ(file->artist, "Written");
    ASSERT_TRUE(file->comment.has_value());
    EXPECT_TRUE(file->comment->empty());
    EXPECT_EQ(file->year, 0);

    patch.year = FieldEdit<std::uint32_t>::clear();
    EXPECT_FALSE(file_metadata_patch(patch));
}

TEST(MetadataPatch, AnyEditedFieldMakesTheRequestNonempty)
{
    StandardMetadataPatch patch;

    EXPECT_TRUE(metadata_patch_empty(patch));

    patch.title = "Edited";
    EXPECT_FALSE(metadata_patch_empty(patch));

    patch = {};
    patch.year = 0;
    EXPECT_FALSE(metadata_patch_empty(patch));
}

TEST(MetadataPatch, AnInvalidEarlyFieldRejectsLaterFieldsAtomically)
{
    StandardFieldOverrides stored;
    stored.artist = "Retained";

    const auto original = stored;
    StandardMetadataPatch patch;
    patch.title = std::string(max_metadata_text_bytes + 1, 'x');
    patch.artist = "Must not apply";
    patch.composer = "Neither may this";
    EXPECT_FALSE(valid_metadata_patch(patch));
    EXPECT_EQ(apply_metadata_patch(stored, patch), MetadataPatchResult::invalid);
    EXPECT_EQ(stored, original);
    EXPECT_FALSE(file_metadata_patch(patch));
}

} // namespace
