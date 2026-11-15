/**
 * @file
 * @brief Stream-specific output ownership and source connection.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
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

#include <engine/pipeline/output_session.hpp>

#include <stdexcept>
#include <utility>

namespace wiola::engine {

struct OutputSession::Stream {
    Stream(pcm::StreamSpec stream_spec, audio::ProcessingChain& chain,
        const OutputFactory& make_output)
        : spec{stream_spec}
        , relay{stream_spec}
        , shaped{relay, chain}
        , output{make_output(shaped)}
    {
        if (!output)
            throw std::runtime_error{"output factory returned no output"};
    }

    pcm::StreamSpec spec;
    audio::Relay relay;
    audio::ShapedSource shaped;
    std::unique_ptr<audio::Output> output;
};

OutputSession::OutputSession(audio::ProcessingChain& chain, OutputFactory make_output) noexcept
    : chain_{chain}
    , make_output_{std::move(make_output)}
{
}

OutputSession::~OutputSession()
{
    disconnect();
}

void OutputSession::open(pcm::StreamSpec spec)
{
    if (open_for(spec))
        return;

    disconnect();
    stream_.reset();
    stream_ = std::make_unique<Stream>(spec, chain_, make_output_);
}

void OutputSession::disconnect() noexcept
{
    if (stream_)
        stream_->relay.point_at_nothing();
}

void OutputSession::connect(audio::Source& source) noexcept
{
    stream_->relay.point_at(source);
}

bool OutputSession::open_for(pcm::StreamSpec spec) const noexcept
{
    return stream_ && stream_->spec == spec;
}

audio::Output& OutputSession::output() const noexcept
{
    return *stream_->output;
}

} // namespace wiola::engine
