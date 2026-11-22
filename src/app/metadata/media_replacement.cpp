/**
 * @file
 * @brief Validated atomic replacement of local media after a metadata write.
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

#include "media_replacement.hpp"

#include <library/fingerprint.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <system_error>

namespace wiola::app {

namespace {

media::MetadataWriteOutcome failure(media::MetadataWriteError error, std::string detail)
{
    return {.state = media::MetadataWriteState::failed,
        .error = error,
        .source_modified = false,
        .detail = std::move(detail)};
}

std::optional<media::MetadataWriteOutcome> compare(media::MediaProvider& provider,
    const std::filesystem::path& source, const std::filesystem::path& temporary,
    const media::OperationContext& context)
{
    auto original_asset = provider.open(source, context);
    auto output_asset = provider.open(temporary, context);

    if (!original_asset || !output_asset)
        return failure(media::MetadataWriteError::invalid_output,
            "could not reopen both media files for validation");

    auto original = (*original_asset)->create_decoder(context);
    auto output = (*output_asset)->create_decoder(context);

    if (!original || !output)
        return failure(media::MetadataWriteError::invalid_output,
            "could not create validation decoders");

    if ((*original)->spec() != (*output)->spec())
        return failure(media::MetadataWriteError::invalid_output,
            "replacement changed the PCM stream format");

    std::array<float, 4096> expected{};
    std::array<float, 4096> actual{};

    for (std::size_t calls = 0; calls < 100000; ++calls) {
        if (context.stop_reason() != media::StopReason::none)
            return failure(media::MetadataWriteError::cancelled, context.error().detail);

        auto left = (*original)->read(expected);
        auto right = (*output)->read(actual);

        if (!left || !right)
            return failure(media::MetadataWriteError::invalid_output,
                "replacement could not be decoded for validation");

        if (left->frames != right->frames)
            return failure(media::MetadataWriteError::invalid_output,
                "replacement changed the decoded frame count");

        const std::size_t samples = (*original)->spec().samples_per(left->frames);
        if (!std::ranges::equal(std::span{expected}.first(samples),
                std::span{actual}.first(samples)))
            return failure(media::MetadataWriteError::invalid_output,
                "replacement changed decoded audio");

        if (left->end_of_stream || right->end_of_stream) {
            if (left->end_of_stream && right->end_of_stream)
                return std::nullopt;

            return failure(media::MetadataWriteError::invalid_output,
                "replacement ended at a different frame");
        }
    }

    return failure(media::MetadataWriteError::invalid_output,
        "validation exceeded the decoder call limit");
}

} // namespace

MediaReplacementValidation prepare_media_replacement(const std::filesystem::path& source,
    const std::filesystem::path& temporary, media::MediaProvider& provider,
    std::string_view expected_fingerprint, const media::OperationContext& context)
{
    const auto failed = [](media::MetadataWriteOutcome outcome) {
        return MediaReplacementValidation{.prepared = {}, .failure = std::move(outcome)};
    };
    if (source.lexically_normal() == temporary.lexically_normal())
        return failed(failure(media::MetadataWriteError::invalid_edit,
            "source and staged output must differ"));
    const auto original = library::fingerprint_media(source);
    if (!original)
        return failed(failure(media::MetadataWriteError::io, "could not fingerprint source"));
    if (!expected_fingerprint.empty() && *original != expected_fingerprint)
        return failed(failure(media::MetadataWriteError::conflicted,
            "source changed before validation"));
    if (auto invalid = compare(provider, source, temporary, context)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return failed(*invalid);
    }
    const auto output = library::fingerprint_media(temporary);
    if (!output)
        return failed(failure(media::MetadataWriteError::invalid_output,
            "could not fingerprint staged output"));
    return {
        .prepared = PreparedMediaReplacement{source, temporary, *original, *output},
        .failure = {}
    };
}

media::MetadataWriteOutcome commit_media_replacement(const std::filesystem::path& source,
    const std::filesystem::path& temporary, media::MediaProvider& provider,
    std::string_view expected, const media::OperationContext& context, MediaReplacementHooks hooks)
{
    auto validation = prepare_media_replacement(source, temporary, provider, expected, context);
    if (!validation.prepared)
        return validation.failure;
    return commit_prepared_media_replacement(*validation.prepared, context, std::move(hooks));
}

media::MetadataWriteOutcome
commit_prepared_media_replacement(const PreparedMediaReplacement& prepared,
    const media::OperationContext& context, MediaReplacementHooks hooks)
{
    const auto& source = prepared.source;
    const auto& temporary = prepared.temporary;
    if (context.stop_reason() != media::StopReason::none)
        return failure(media::MetadataWriteError::cancelled, context.error().detail);
    if (library::fingerprint_media(source) != std::optional{prepared.source_fingerprint} ||
        library::fingerprint_media(temporary) != std::optional{prepared.output_fingerprint})
        return failure(media::MetadataWriteError::conflicted,
            "source or staged output changed after validation");
    std::filesystem::path backup = source;
    backup += ".wiola-backup";

    std::error_code error;
    if (std::filesystem::exists(backup, error))
        return failure(media::MetadataWriteError::invalid_output,
            "a previous media backup already exists");

    if (error)
        return failure(media::MetadataWriteError::io, error.message());

    if (hooks.before_commit && !hooks.before_commit())
        return failure(media::MetadataWriteError::io,
            "the source is locked or replacement was refused");

    if (context.stop_reason() != media::StopReason::none)
        return failure(media::MetadataWriteError::cancelled, context.error().detail);
    if (library::fingerprint_media(source) != std::optional{prepared.source_fingerprint})
        return failure(media::MetadataWriteError::conflicted,
            "source changed at replacement commit");

#ifdef _WIN32
    std::filesystem::rename(source, backup, error);
    if (error)
        return failure(media::MetadataWriteError::io, error.message());

    std::filesystem::rename(temporary, source, error);
    if (error) {
        std::error_code rollback;
        std::filesystem::rename(backup, source, rollback);
        return failure(media::MetadataWriteError::io, error.message());
    }
#else
    std::filesystem::copy_file(source, backup, std::filesystem::copy_options::none, error);
    if (error) {
        const std::string detail = error.message();
        std::error_code ignored;
        std::filesystem::remove(backup, ignored);
        return failure(media::MetadataWriteError::io, detail);
    }

    std::filesystem::rename(temporary, source, error);
    if (error)
        return failure(media::MetadataWriteError::io, error.message());
#endif

    return {.state = media::MetadataWriteState::written,
        .error = media::MetadataWriteError::none,
        .source_modified = true,
        .detail = source.string()};
}

} // namespace wiola::app
