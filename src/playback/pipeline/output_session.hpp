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
#include <wiola/media/stream_spec.hpp>
#include <wiola/playback/output_factory.hpp>

#include <memory>
#include <stdexcept>

namespace wiola::audio {

class Stage;
class Source;

} // namespace wiola::audio

namespace wiola::engine {

/// Prepares stream-specific output leases and caches the last committed stream for reuse.
/// The borrowed processing stage must outlive both this cache and every prepared lease.
class OutputSession {
    struct Stream;

public:
    /// Output construction failed. Allocation failure keeps its original exceptional policy.
    class Unavailable final : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    /// Pins the output and its render bridge. Its stable identity owns one source attachment.
    /// Quiesce callbacks before destruction; PlaybackPipeline enforces this through member order.
    class Lease {
    public:
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&&) = delete;
        Lease& operator=(Lease&&) = delete;
        ~Lease();

        [[nodiscard]] audio::Output& output() const noexcept;

    private:
        friend class OutputSession;
        explicit Lease(std::shared_ptr<Stream> stream) noexcept;

        std::shared_ptr<Stream> stream_;
    };

    OutputSession(audio::Stage& processing, OutputFactory make_output) noexcept;
    ~OutputSession();

    OutputSession(const OutputSession&) = delete;
    OutputSession& operator=(const OutputSession&) = delete;
    OutputSession(OutputSession&&) = delete;
    OutputSession& operator=(OutputSession&&) = delete;

    /// Construct a stopped output or reuse the committed stream. Failure/discard changes
    /// neither the cached stream nor its active source. The returned lease is never null.
    [[nodiscard]] std::unique_ptr<Lease> prepare(pcm::StreamSpec spec);

    /// Control thread, after prior callbacks stop: retain this stream and attach its source.
    /// The lease and source outlive callbacks; a superseded lease cannot detach its successor.
    void activate(Lease& lease, audio::Source& source) noexcept;

    [[nodiscard]] bool open_for(pcm::StreamSpec spec) const noexcept;

private:
    audio::Stage& processing_;
    OutputFactory make_output_;
    std::shared_ptr<Stream> stream_;
};

} // namespace wiola::engine
