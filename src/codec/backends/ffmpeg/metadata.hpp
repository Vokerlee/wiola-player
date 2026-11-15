/**
 * @file
 * @brief Private bounded FFmpeg metadata and attached-artwork fallback.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#pragma once

#include <media/media.hpp>

#include <filesystem>

namespace wiola::codec::ffmpeg {

[[nodiscard]] media::Result<media::Metadata> read_metadata(const std::filesystem::path& path);

} // namespace wiola::codec::ffmpeg
