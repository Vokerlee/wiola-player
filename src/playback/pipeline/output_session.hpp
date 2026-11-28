/**
 * @file
 * @brief Stream-specific output ownership and source connection.
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

#include <wiola/audio/output.hpp>
#include <wiola/foundation/lease.hpp>
#include <wiola/media/stream_spec.hpp>
#include <wiola/playback/output_factory.hpp>

#include <memory>
#include <optional>
#include <stdexcept>

namespace wiola::audio {

class Stage;
class Source;

} // namespace wiola::audio

namespace wiola::engine {

/// Prepares stream-specific output leases and caches the last committed stream for reuse.
/// Retained processing dependencies survive the cache through prepared output leases.
class OutputSession {
    struct Stream;

    struct Identity { };

public:
    /// Output construction failed. Allocation failure keeps its original exceptional policy.
    class Unavailable final : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    /// Pins the output and its render bridge. Its stable identity owns one source attachment.
    /// Destruction detaches first. Source retirement waits for stopped exclusive permission;
    /// an independently held controller keeps the retired source pinned through its bridge.
    class Attachment {
    public:
        Attachment(const Attachment&) = delete;
        Attachment& operator=(const Attachment&) = delete;
        Attachment(Attachment&&) = delete;
        Attachment& operator=(Attachment&&) = delete;
        ~Attachment();

        [[nodiscard]] core::Lease<audio::Output> output() const noexcept;

        /// Transfers setup permission when this attachment prepared an unused output.
        [[nodiscard]] std::optional<audio::Output::Quiescence> take_quiescence() noexcept;

    private:
        friend class OutputSession;
        explicit Attachment(core::Lease<Stream> stream,
            std::optional<audio::Output::Quiescence> permission) noexcept;

        core::Lease<Stream> stream_;
        std::optional<audio::Output::Quiescence> permission_;
    };

    OutputSession(core::Lease<audio::Stage> processing, OutputFactory make_output);
    ~OutputSession();

    OutputSession(const OutputSession&) = delete;
    OutputSession& operator=(const OutputSession&) = delete;
    OutputSession(OutputSession&&) = delete;
    OutputSession& operator=(OutputSession&&) = delete;

    /// Construct a stopped output or reuse the committed stream. Failure/discard changes
    /// neither the cached stream nor its active source. The returned lease is never null.
    [[nodiscard]] std::unique_ptr<Attachment> prepare(pcm::StreamSpec spec);

    /// Control thread with actual stopped permission for this output: retain and attach source.
    /// Wrong cache, permission or stream shape changes nothing and does not consume permission.
    [[nodiscard]] bool activate(Attachment& attachment, core::Lease<audio::Source> source,
        const audio::Output::Quiescence& permission) noexcept;

    [[nodiscard]] bool open_for(pcm::StreamSpec spec) const noexcept;

private:
    core::Lease<const Identity> identity_ = core::Lease<const Identity>::make();
    core::Lease<audio::Stage> processing_;
    OutputFactory make_output_;
    std::optional<core::Lease<Stream>> stream_;
};

} // namespace wiola::engine
