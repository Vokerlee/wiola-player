/**
 * @file
 * @brief Independent target consumer of the audio output contract.
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

#include <wiola/audio/output.hpp>
#include <wiola/audio/output_connection.hpp>
#include <wiola/audio/source.hpp>

#include <algorithm>
#include <array>
#include <type_traits>

namespace {

class ConstantSource final : public wiola::audio::Source {
public:
    std::size_t render(std::span<float> output) noexcept override
    {
        std::ranges::fill(output, 0.5F);
        return output.size();
    }

    wiola::pcm::StreamSpec spec() const noexcept override { return {}; }
};

class ControlledOutput final : public wiola::audio::Output {
public:
    explicit ControlledOutput(wiola::audio::Source& source)
        : source_{source}
    {
    }

    bool running() const noexcept override { return running_; }

    wiola::pcm::Frames frames_played() const noexcept override { return frames_; }

private:
    bool start() noexcept override
    {
        std::array<float, 2> samples{};
        frames_ = source_.spec().frames_per(source_.render(samples));
        running_ = samples[0] == 0.5F;

        return running_;
    }

    void stop() noexcept override { running_ = false; }

    wiola::audio::Source& source_;
    wiola::pcm::Frames frames_;
    bool running_{};
};

static_assert(!std::is_copy_constructible_v<ControlledOutput>);
static_assert(!std::is_move_constructible_v<wiola::audio::OutputConnection>);

} // namespace

int main()
{
    const auto source = wiola::core::Lease<ConstantSource>::make();
    auto output = wiola::core::Lease<ControlledOutput>::dependent(
        [](ConstantSource& source) { return std::make_unique<ControlledOutput>(source); }, source
    );
    {
        wiola::audio::OutputConnection connection{output};

        if (!connection.try_acquire())
            return 4;

        if (!connection.control().start() || output->frames_played() != wiola::pcm::Frames{1})
            return 1;

        if (wiola::audio::Output::acquire(output).start())
            return 2;
    }

    if (output->running())
        return 3;

    return !wiola::audio::Output::acquire(output).start();
}
