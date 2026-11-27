/**
 * @file
 * @brief Validated atomic media replacement regression tests.
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
#include <wiola/media/local/file_replacement.hpp>
#include <wiola/media/local/fingerprint.hpp>

#include <gtest/gtest.h>

#include <fstream>

namespace {

using namespace wiola::media;

struct Files {
    wiola::testing::TemporaryDirectory directory;
    std::filesystem::path source, output, backup;

    Files()
        : source{directory.path() / std::filesystem::path{u8"музыка.mp3"}}
        , output{directory.path() / "staged.mp3"}
        , backup{directory.path() / "backup.mp3"}
    {
        std::ofstream{source} << "original bytes";
        std::ofstream{output} << "replacement bytes";
    }

    FileReplacementRequest request() const
    {
        return {source, output, backup, *wiola::media::fingerprint_media(source),
            *wiola::media::fingerprint_media(output)};
    }
};

TEST(FileReplacement, EveryInterruptedBoundaryLeavesKnownSourceOrVerifiedRecovery)
{
    for (const auto boundary : {FileReplacementStep::journal_created,
             FileReplacementStep::before_backup, FileReplacementStep::backup_created,
             FileReplacementStep::before_replace, FileReplacementStep::replaced}) {
        SCOPED_TRACE(static_cast<int>(boundary));

        Files files;
        const auto request = files.request();
        auto port = native_file_replacement(wiola::media::fingerprint_media,
            [&](auto step) { return step != boundary; });
        const auto result = port->replace(request);

        EXPECT_NE(result.error, FileReplacementError::none);

        const auto source = wiola::media::fingerprint_media(files.source);

        EXPECT_TRUE(source == std::optional{request.original_digest} ||
            source == std::optional{request.output_digest});

        auto recovery = native_file_replacement(wiola::media::fingerprint_media);
        const auto recovered = recovery->recover(files.source);

        EXPECT_EQ(recovered.error, FileReplacementError::none);

        const auto after = wiola::media::fingerprint_media(files.source);

        EXPECT_TRUE(after == std::optional{request.original_digest} ||
            after == std::optional{request.output_digest});

        if (std::filesystem::exists(files.backup))
            EXPECT_EQ(wiola::media::fingerprint_media(files.backup), request.original_digest);
    }
}

TEST(FileReplacement, RechecksSourceAfterBackupAndNeverOverwritesAnExternalEdit)
{
    Files files;
    const auto request = files.request();
    auto port = native_file_replacement(wiola::media::fingerprint_media, [&](auto step) {
        if (step == FileReplacementStep::before_replace)
            std::ofstream{files.source, std::ios::app} << "external";
        return true;
    });
    const auto result = port->replace(request);

    EXPECT_EQ(result.error, FileReplacementError::conflict);
    EXPECT_FALSE(result.source_modified);

    const auto external = wiola::media::fingerprint_media(files.source);

    EXPECT_NE(external, request.original_digest);
    EXPECT_NE(external, request.output_digest);
    EXPECT_EQ(port->recover(files.source).error, FileReplacementError::recovery_required);
    EXPECT_EQ(wiola::media::fingerprint_media(files.source), external);
}

TEST(FileReplacement, ExistingBackupsAndUnknownOperationRecordsRemainIntact)
{
    Files files;
    const auto request = files.request();

    std::ofstream{files.backup} << "previous backup";

    const auto backup = wiola::media::fingerprint_media(files.backup);
    auto port = native_file_replacement(wiola::media::fingerprint_media);

    EXPECT_EQ(port->replace(request).error, FileReplacementError::recovery_required);
    EXPECT_EQ(wiola::media::fingerprint_media(files.backup), backup);

    auto journal = files.source;
    journal += ".wiola-replacement";
    std::ofstream{journal} << "wiola-replacement 999\n";
    EXPECT_EQ(port->recover(files.source).error, FileReplacementError::recovery_required);
    EXPECT_TRUE(std::filesystem::exists(journal));
    EXPECT_EQ(wiola::media::fingerprint_media(files.source), request.original_digest);
}
#ifndef _WIN32
TEST(FileReplacement, MissingSourceRecoversOnlyFromVerifiedBackupAndPreservesPermissions)
{
    Files files;
    const auto request = files.request();
    auto port = native_file_replacement(wiola::media::fingerprint_media,
        [](auto step) { return step != FileReplacementStep::backup_created; });

    EXPECT_NE(port->replace(request).error, FileReplacementError::none);
    ASSERT_TRUE(std::filesystem::exists(files.backup));

    std::filesystem::remove(files.source);

    auto recovery = native_file_replacement(wiola::media::fingerprint_media);
    const auto result = recovery->recover(files.source);

    EXPECT_TRUE(result.recovered);
    EXPECT_EQ(result.error, FileReplacementError::none);
    EXPECT_EQ(wiola::media::fingerprint_media(files.source), request.original_digest);
    EXPECT_TRUE(std::filesystem::exists(files.backup));

    Files committed;
    const auto permissions =
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
    std::filesystem::permissions(committed.source, permissions);
    EXPECT_EQ(recovery->replace(committed.request()).error, FileReplacementError::none);
    EXPECT_EQ(std::filesystem::status(committed.source).permissions(), permissions);
}
#endif
TEST(FileReplacement, OversizedRecoveryRecordIsRejectedWithoutChangingAnyFile)
{
    Files files;
    const auto request = files.request();
    auto journal = files.source;
    journal += ".wiola-replacement";
    std::ofstream{journal} << std::string(64 * 1024 + 1, 'x');

    auto port = native_file_replacement(wiola::media::fingerprint_media);

    EXPECT_EQ(port->recover(files.source).error, FileReplacementError::recovery_required);
    EXPECT_EQ(wiola::media::fingerprint_media(files.source), request.original_digest);
    EXPECT_EQ(wiola::media::fingerprint_media(files.output), request.output_digest);
}

} // namespace
