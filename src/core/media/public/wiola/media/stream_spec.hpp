/**
 * @file
 * @brief Description of an interleaved PCM stream.
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

#include <wiola/units.hpp>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

namespace wiola::pcm {

/// A count of frames. Samples are counted plainly, being the length of a span; a frame is what
/// gets mistaken for one.
class Frames {
public:
    constexpr Frames() noexcept = default;

    constexpr explicit Frames(std::size_t count) noexcept
        : count_{count}
    {
    }

    [[nodiscard]] constexpr std::size_t count() const noexcept { return count_; }

    constexpr Frames& operator+=(Frames other) noexcept
    {
        count_ += other.count_;
        return *this;
    }

    friend constexpr auto operator<=>(Frames, Frames) = default;

    friend constexpr Frames operator+(Frames a, Frames b) noexcept
    {
        return Frames{a.count_ + b.count_};
    }

    friend constexpr Frames operator-(Frames a, Frames b) noexcept
    {
        return Frames{a.count_ - b.count_};
    }

private:
    std::size_t count_{0};
};

/**
 * Interleaved PCM, uncompressed: one measurement of one channel is a sample, one sample per
 * channel at the same instant is a frame, and `sample_rate` counts frames per second.
 *
 *     stereo, 4 frames:
 *
 *     frame 0     frame 1     frame 2     frame 3
 *     [ L  R ]    [ L  R ]    [ L  R ]    [ L  R ]
 *       ^  ^
 *       2 samples = 1 frame
 *
 * The other layout is planar: one array per channel, [ L L L L ] [ R R R R ]. Codecs commonly
 * decode that way. Everything here is interleaved, which is what a device callback takes.
 */
enum class ChannelLayout {
    unspecified,
    mono,
    stereo,
    five_point_one_side,
};

struct StreamSpec {
    units::Frequency sample_rate{48000.0};
    std::size_t num_channels{2};
    ChannelLayout channel_layout{ChannelLayout::unspecified};

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return std::isfinite(sample_rate.hz()) && sample_rate > units::Frequency{} &&
            num_channels > 0;
    }

    /// Internal arithmetic for a stream whose validity and representable result are established.
    [[nodiscard]] constexpr std::size_t samples_per(Frames num_frames) const noexcept
    {
        assert(num_channels > 0);
        assert(num_frames.count() <= std::numeric_limits<std::size_t>::max() / num_channels);
        return num_frames.count() * num_channels;
    }

    [[nodiscard]] constexpr Frames frames_per(std::size_t num_samples) const noexcept
    {
        assert(num_channels > 0);
        return Frames{num_samples / num_channels};
    }

    /// Checked external conversion: no multiplication overflow or invalid stream arithmetic.
    [[nodiscard]] constexpr std::optional<std::size_t>
    checked_samples_per(Frames num_frames) const noexcept
    {
        if (!valid() || num_frames.count() > std::numeric_limits<std::size_t>::max() / num_channels)
            return std::nullopt;

        return samples_per(num_frames);
    }

    /// How long `num_frames` take to play.
    [[nodiscard]] constexpr units::Time time_per(Frames num_frames) const noexcept
    {
        assert(valid());
        return units::Time{static_cast<double>(num_frames.count()) / sample_rate.get<units::Hz>()};
    }

    /// Checked conversion of an external position or duration to a whole frame count.
    [[nodiscard]] constexpr std::optional<Frames>
    checked_frames_at(units::Time position) const noexcept
    {
        if (!valid() || !std::isfinite(position.seconds()) || position < units::Time{})
            return std::nullopt;

        const double frames{position * sample_rate};
        if (!position_is_representable(frames))
            return std::nullopt;

        return Frames{static_cast<std::size_t>(frames)};
    }

    /// Checked conversion of a duration to a sample capacity.
    [[nodiscard]] constexpr std::optional<std::size_t>
    checked_samples_per(units::Time duration) const noexcept
    {
        const std::optional<Frames> frames{checked_frames_at(duration)};

        return frames ? checked_samples_per(*frames) : std::nullopt;
    }

    [[nodiscard]] constexpr std::size_t samples_per(units::Time duration) const noexcept
    {
        const double frames{duration * sample_rate};
        assert(valid());
        assert(position_is_representable(frames));
        return samples_per(Frames{static_cast<std::size_t>(frames)});
    }

    friend constexpr bool operator==(const StreamSpec&, const StreamSpec&) = default;

private:
    [[nodiscard]] static constexpr bool position_is_representable(double frames) noexcept
    {
        return std::isfinite(frames) && frames >= 0.0 &&
            frames < static_cast<double>(std::numeric_limits<std::size_t>::max());
    }
};

} // namespace wiola::pcm
