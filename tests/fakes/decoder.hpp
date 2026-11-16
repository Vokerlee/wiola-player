/**
 * @file
 * @brief Controllable decoder and one-shot synchronization gate for engine tests.
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

#pragma once

#include <codec/decode/decoder.hpp>
#include <pcm/stream_spec.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <limits>
#include <mutex>
#include <span>

namespace wiola::testing {

/// Holds one collaborator call until the test releases it, and says when the call has arrived.
class Gate {
public:
    void wait()
    {
        std::unique_lock lock{mutex_};
        entered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [this] { return released_; });
    }

    void wait_until_entered()
    {
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [this] { return entered_; });
    }

    void release()
    {
        const std::lock_guard lock{mutex_};
        released_ = true;
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool entered_{false};
    bool released_{false};
};

/// A silent decoder whose read and seek calls can be held or refused independently.
class FakeDecoder final : public codec::Decoder {
public:
    explicit FakeDecoder(pcm::Frames num_frames = pcm::Frames{4096},
        pcm::StreamSpec spec = {}) noexcept
        : codec::AudioDecoder{spec, num_frames}
    {
    }

    void block_reads_on(Gate& gate) noexcept { read_gate_ = &gate; }

    void block_seeks_on(Gate& gate) noexcept { seek_gate_ = &gate; }

    void refuse_seeks() noexcept { refusing_seeks_.store(true); }

    void refuse_next_seek() noexcept { refuse_next_seek_.store(true); }

    void achieve_next_seek_at(pcm::Frames achieved) noexcept
    {
        next_achieved_.store(achieved.count());
    }

    [[nodiscard]] int num_reads() const noexcept { return num_reads_.load(); }

    [[nodiscard]] std::size_t last_render_samples() const noexcept
    {
        return last_render_samples_.load();
    }

    [[nodiscard]] int num_seeks() const noexcept { return num_seeks_.load(); }

    [[nodiscard]] pcm::Frames last_seek() const noexcept { return pcm::Frames{last_seek_.load()}; }

protected:
    media::Result<codec::DecodeOutcome> decode(std::span<float> output,
        pcm::Frames num_frames) override
    {
        if (read_gate_ != nullptr)
            read_gate_->wait();

        num_reads_.fetch_add(1);
        last_render_samples_.store(spec().samples_per(num_frames));
        std::fill_n(output.begin(), spec().samples_per(num_frames), 0.0F);

        return codec::DecodeOutcome{.frames = num_frames};
    }

    media::Result<codec::SeekOutcome> seek_frame(pcm::Frames frame_index) override
    {
        num_seeks_.fetch_add(1);
        last_seek_.store(frame_index.count());

        if (seek_gate_ != nullptr)
            seek_gate_->wait();

        const bool refused{refusing_seeks_.load() || refuse_next_seek_.exchange(false)};
        if (refused)
            return media::unexpected{
                media::Error{.category = media::ErrorCategory::seek_failed,
                             .detail = "seek refused"}
            };

        constexpr std::size_t use_target{std::numeric_limits<std::size_t>::max()};
        const std::size_t requested{next_achieved_.exchange(use_target)};
        const pcm::Frames achieved{requested == use_target ? frame_index.count() : requested};
        return codec::SeekOutcome{.target = frame_index,
            .achieved = achieved,
            .exact = achieved == frame_index};
    }

private:
    Gate* read_gate_{nullptr};
    Gate* seek_gate_{nullptr};
    std::atomic<bool> refusing_seeks_{false};
    std::atomic<bool> refuse_next_seek_{false};
    std::atomic<int> num_reads_{0};
    std::atomic<int> num_seeks_{0};
    std::atomic<std::size_t> last_seek_{0};
    std::atomic<std::size_t> last_render_samples_{0};
    std::atomic<std::size_t> next_achieved_{std::numeric_limits<std::size_t>::max()};
};

} // namespace wiola::testing
