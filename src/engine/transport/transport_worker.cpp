/**
 * @file
 * @brief Owns transport commands and the thread that performs blocking playback work.
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

#include "transport_worker.hpp"

#include <stdexcept>
#include <utility>

namespace wiola::engine {

namespace {

const TransportConfig& validated(const TransportConfig& config)
{
    if (!config.valid())
        throw std::invalid_argument{"invalid transport configuration"};

    return config;
}

} // namespace

TransportWorker::TransportWorker(std::unique_ptr<media::AudioDecoder> source,
    pcm::PcmQueue::Producer buffer, audio::Output& output, ThreadFactory launch,
    TransportConfig config)
    : head_{source->spec().num_channels}
    , wait_{validated(config).poll_interval}
    , loop_{std::move(source), std::move(buffer), output, playback_, head_.applier(), wait_, config}
    , launch_{std::move(launch)}
{
    if (!launch_)
        launch_ = [](std::function<void()> work) {
            return std::jthread{std::move(work)};
        };
}

TransportWorker::~TransportWorker()
{
    stop();
    wait();
}

bool TransportWorker::start()
{
    const Playback::State previous{playback_.state()};

    if (previous == Playback::State::playing || previous == Playback::State::paused)
        return false;

    if (thread_.joinable())
        thread_.join();

    // Replay reports its beginning immediately; the worker carries out the actual rewind.
    if (previous != Playback::State::idle && !head_.seek_outstanding())
        head_.request_seek(pcm::Frames{});

    if (!playback_.begin())
        return false;

    try {
        thread_ = launch_([this, rewind = previous != Playback::State::idle] { work(rewind); });
    } catch (...) {
        playback_.finish(Playback::TerminalReason::faulted);
        return false;
    }

    return true;
}

bool TransportWorker::pause() noexcept
{
    const bool accepted{playback_.pause()};
    wait_.notify();
    return accepted;
}

bool TransportWorker::resume() noexcept
{
    const bool accepted{playback_.resume()};
    wait_.notify();
    return accepted;
}

void TransportWorker::stop() noexcept
{
    playback_.finish(Playback::TerminalReason::stopped);
    wait_.notify();
}

void TransportWorker::seek(pcm::Frames target) noexcept
{
    head_.request_seek(target);
    wait_.notify();
}

void TransportWorker::wait()
{
    if (thread_.joinable())
        thread_.join();
}

Playback::State TransportWorker::state() const noexcept
{
    return playback_.state();
}

bool TransportWorker::playing() const noexcept
{
    return playback_.playing();
}

bool TransportWorker::finished() const noexcept
{
    return playback_.finished();
}

pcm::Frames TransportWorker::position(pcm::Frames frames_played) const noexcept
{
    return head_.position(frames_played);
}

void TransportWorker::work(bool rewind) noexcept
{
    if (!loop_.begin(rewind)) {
        playback_.finish(Playback::TerminalReason::faulted);
        return;
    }

    loop_.run();
}

} // namespace wiola::engine
