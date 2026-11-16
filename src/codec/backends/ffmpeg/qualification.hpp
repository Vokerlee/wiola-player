/**
 * @file
 * @brief Private machine-readable FFmpeg container/codec/profile qualification set.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#pragma once

#include <span>
#include <string_view>

namespace wiola::codec::ffmpeg {

struct Qualification {
    std::string_view format;
    std::string_view container;
    std::string_view codec;
    std::string_view profile;
};

[[nodiscard]] std::span<const Qualification> qualifications() noexcept;
[[nodiscard]] bool is_qualified(std::string_view container, std::string_view codec,
    std::string_view profile) noexcept;

} // namespace wiola::codec::ffmpeg
