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

#include <media/local/resource.hpp>

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
    auto original_asset = provider.open(wiola::media::local_resource(source), context);
    auto output_asset = provider.open(wiola::media::local_resource(temporary), context);

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

    std::size_t left_size{}, right_size{}, left_offset{}, right_offset{};
    bool left_end{}, right_end{};
    std::size_t no_progress{};
    for (std::size_t calls = 0; calls < 10000000; ++calls) {
        if (context.stop_reason() != media::StopReason::none)
            return failure(media::MetadataWriteError::cancelled, context.error().detail);
        if (left_offset == left_size && !left_end) {
            const auto read = (*original)->read(expected);
            if (!read)
                return failure(media::MetadataWriteError::invalid_output, "source decoder failed");
            left_size = (*original)->spec().samples_per(read->frames);
            left_offset = 0;
            left_end = read->end_of_stream;
            if (left_size > expected.size())
                return failure(media::MetadataWriteError::invalid_output,
                    "source decoder exceeded its buffer");
        }
        if (right_offset == right_size && !right_end) {
            const auto read = (*output)->read(actual);
            if (!read)
                return failure(media::MetadataWriteError::invalid_output, "output decoder failed");
            right_size = (*output)->spec().samples_per(read->frames);
            right_offset = 0;
            right_end = read->end_of_stream;
            if (right_size > actual.size())
                return failure(media::MetadataWriteError::invalid_output,
                    "output decoder exceeded its buffer");
        }
        const auto count = std::min(left_size - left_offset, right_size - right_offset);
        if (!std::ranges::equal(std::span{expected}.subspan(left_offset, count),
                std::span{actual}.subspan(right_offset, count)))
            return failure(media::MetadataWriteError::invalid_output,
                "replacement changed decoded audio");
        left_offset += count;
        right_offset += count;
        if (count == 0) {
            if (++no_progress > 64)
                return failure(media::MetadataWriteError::invalid_output,
                    "decoder made no validation progress");
        } else
            no_progress = 0;
        const bool left_done = left_end && left_offset == left_size,
                   right_done = right_end && right_offset == right_size;
        if (left_done && right_done)
            return std::nullopt;
        if ((left_done && right_size > right_offset) || (right_done && left_size > left_offset))
            return failure(media::MetadataWriteError::invalid_output,
                "replacement ended at a different frame");
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
    if (hooks.before_commit && !hooks.before_commit())
        return failure(media::MetadataWriteError::io,
            "source is locked or replacement was refused");
    auto backup = source;
    backup += ".wiola-backup";
    if (!hooks.backup_suffix.empty())
        backup += "." + hooks.backup_suffix;
    auto native = media::native_file_replacement(library::fingerprint_media);
    auto& port = hooks.replacement ? *hooks.replacement : *native;
    const auto outcome = port.replace({source, temporary, backup, prepared.source_fingerprint,
                                          prepared.output_fingerprint},
        [&] { return context.stop_reason() == media::StopReason::none; });
    if (outcome.error != media::FileReplacementError::none) {
        auto result = failure(outcome.error == media::FileReplacementError::conflict
                ? media::MetadataWriteError::conflicted
                : outcome.error == media::FileReplacementError::cancelled &&
                    context.stop_reason() != media::StopReason::none
                ? media::MetadataWriteError::cancelled
                : media::MetadataWriteError::io,
            outcome.detail);
        result.source_modified = outcome.source_modified;
        return result;
    }
    return {.state = media::MetadataWriteState::written,
        .error = media::MetadataWriteError::none,
        .source_modified = true,
        .detail = source.string()};
}

} // namespace wiola::app
