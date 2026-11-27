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

#include "output_session.hpp"

#include <buffering/relay.hpp>
#include <buffering/shaped_source.hpp>

#include <exception>
#include <new>
#include <utility>

namespace wiola::engine {

namespace {

std::unique_ptr<audio::Output> construct_output(audio::Source& source,
    const OutputFactory& make_output)
{
    std::unique_ptr<audio::Output> output;

    try {
        output = make_output(source);
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& failure) {
        throw OutputSession::Unavailable{failure.what()};
    } catch (...) {
        throw OutputSession::Unavailable{"output factory threw an unknown exception"};
    }

    if (!output)
        throw OutputSession::Unavailable{"output factory returned no output"};

    return output;
}

} // namespace

struct OutputSession::Stream {
    struct Bridge {
        Bridge(pcm::StreamSpec spec, audio::Stage& processing)
            : relay{spec}
            , shaped{relay, processing}
        {
        }

        Relay relay;
        ShapedSource shaped;
    };

    Stream(pcm::StreamSpec spec, core::Lease<audio::Stage> processing,
        const OutputFactory& make_output)
        : bridge{core::Lease<Bridge>::dependent(
              [spec](audio::Stage& stage) { return std::make_unique<Bridge>(spec, stage); },
              processing
          )}
        , output{core::Lease<audio::Output>::dependent(
              [&make_output](Bridge& source) {
                  return construct_output(source.shaped, make_output);
              },
              bridge
          )}
    {
    }

    core::Lease<Bridge> bridge;
    core::Lease<audio::Output> output;
    const Lease* attached{nullptr};
};

OutputSession::OutputSession(core::Lease<audio::Stage> processing,
    OutputFactory make_output) noexcept
    : processing_{processing}
    , make_output_{std::move(make_output)}
{
}

OutputSession::~OutputSession() = default;

OutputSession::Lease::Lease(core::Lease<Stream> stream) noexcept
    : stream_{std::move(stream)}
{
}

OutputSession::Lease::~Lease()
{
    if (stream_->attached == this) {
        stream_->bridge->relay.point_at_nothing();
        stream_->attached = nullptr;
    }
}

core::Lease<audio::Output> OutputSession::Lease::output() const noexcept
{
    return stream_->output;
}

std::unique_ptr<OutputSession::Lease> OutputSession::prepare(pcm::StreamSpec spec)
{
    auto stream{
        open_for(spec) ? *stream_ : core::Lease<Stream>::make(spec, processing_, make_output_)};
    return std::unique_ptr<Lease>{new Lease{std::move(stream)}};
}

void OutputSession::activate(Lease& lease, audio::Source& source) noexcept
{
    stream_ = lease.stream_;
    (*stream_)->attached = &lease;
    (*stream_)->bridge->relay.point_at(source);
}

bool OutputSession::open_for(pcm::StreamSpec spec) const noexcept
{
    return stream_ && (*stream_)->bridge->relay.spec() == spec;
}

} // namespace wiola::engine
