/**
 * @file
 * @brief Captured metadata edit targets, expected state, mixed fields and draft.
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

#pragma once
#include <library/identity.hpp>
#include <library/metadata_patch.hpp>
#include <media/media.hpp>
#include <media/operation_context.hpp>
#include <memory>
#include <stop_token>

namespace wiola::app {

struct MetadataWriteOperationId {
    std::uint64_t value{};

    bool valid() const { return value != 0; }

    auto operator<=>(const MetadataWriteOperationId&) const = default;
};
enum class MetadataWriteStage {
    queued,
    remuxing,
    validating,
    ready,
    reloading,
    complete,
    conflicted,
    cancelled,
    failed,
};

struct MetadataWriteJob {
    MetadataWriteOperationId operation;
    library::MediaId media;
    std::filesystem::path source;
    std::uint64_t generation{};
    media::ExpectedMediaState expected_source;
    std::uint64_t accepted_edit_revision{};
    library::StandardMetadataPatch patch;
    media::MetadataWriteCapabilities capabilities;
    std::shared_ptr<std::stop_source> cancellation{std::make_shared<std::stop_source>()};

    [[nodiscard]] media::OperationContext context() const
    {
        return {.stop = cancellation->get_token(), .deadline = {}};
    }
};

/// Owns only a writer-created output. Destruction never removes the original source.
class StagedMetadataOutput final {
public:
    explicit StagedMetadataOutput(std::filesystem::path path)
        : path_{std::move(path)}
    {
    }

    ~StagedMetadataOutput();
    StagedMetadataOutput(const StagedMetadataOutput&) = delete;
    StagedMetadataOutput& operator=(const StagedMetadataOutput&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

struct MetadataWriteResult {
    MetadataWriteJob job;
    MetadataWriteStage stage{MetadataWriteStage::failed};
    media::MetadataWriteOutcome outcome;
    std::shared_ptr<StagedMetadataOutput> output;
    std::string source_fingerprint;
};

} // namespace wiola::app
