/**
 * @file
 * @brief Bounded corruption and truncation smoke tests for FFmpeg decoding.
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

#include <media/codecs/ffmpeg/decoder.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

class TempDirectory final {
public:
    TempDirectory()
        : path_{std::filesystem::current_path() / "ffmpeg-hardening-temp"}
    {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() { std::filesystem::remove_all(path_); }

    std::filesystem::path file(const std::string& name) const { return path_ / name; }

private:
    std::filesystem::path path_;
};

std::vector<std::byte> read_bytes(const std::filesystem::path& path)
{
    std::ifstream input{path, std::ios::binary};
    const std::vector<char> chars{std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    std::vector<std::byte> bytes(chars.size());
    std::ranges::transform(chars, bytes.begin(),
        [](char value) { return static_cast<std::byte>(static_cast<unsigned char>(value)); });
    return bytes;
}

void write_bytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes)
{
    std::ofstream output{path, std::ios::binary};
    output.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
}

void exercise(const std::filesystem::path& path)
{
    auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(path)};
    if (!decoder)
        return;
    std::array<float, 256> block{};
    for (int calls{0}; calls < 256; ++calls) {
        auto read{(*decoder)->read(block)};
        if (!read)
            return;
        if (read->frames == wiola::pcm::Frames{} && !read->end_of_stream) {
            ADD_FAILURE() << "zero-frame nonterminal read for " << path.filename();
            return;
        }
        if (read->end_of_stream)
            return;
    }
    ADD_FAILURE() << "decode did not reach a bounded outcome for " << path.filename();
}

TEST(FfmpegHardening, TruncatedContainersFailOrDrainWithoutStalling)
{
    TempDirectory temp;
    for (const char* name : {"tone.wav", "tone-vorbis.ogg", "multi-audio.m4a", "tone.ape"}) {
        const auto bytes{read_bytes(std::filesystem::path{WIOLA_TEST_DATA_DIR} / name)};
        ASSERT_FALSE(bytes.empty()) << name;
        for (const std::size_t size : {std::size_t{1}, bytes.size() / 2, bytes.size() - 1}) {
            std::vector<std::byte> truncated{bytes.begin(),
                bytes.begin() + static_cast<std::ptrdiff_t>(size)};
            const auto path{temp.file(name + std::to_string(size))};
            write_bytes(path, truncated);
            exercise(path);
        }
    }
}

TEST(FfmpegHardening, DeterministicOggMutationsHaveBoundedOutcomes)
{
    TempDirectory temp;
    const auto original{read_bytes(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone-vorbis.ogg")};
    ASSERT_GT(original.size(), 32U);
    for (std::size_t variant{0}; variant < 32; ++variant) {
        auto bytes{original};
        std::uint32_t state{static_cast<std::uint32_t>(variant + 1)};
        const std::size_t step{std::max<std::size_t>(bytes.size() / 64, 1)};
        for (std::size_t index{16}; index < bytes.size(); index += step) {
            state = state * 1664525U + 1013904223U;
            bytes[index] ^= static_cast<std::byte>(state >> 24);
        }
        const auto path{temp.file("mutated-" + std::to_string(variant) + ".ogg")};
        write_bytes(path, bytes);
        exercise(path);
    }
}

} // namespace
