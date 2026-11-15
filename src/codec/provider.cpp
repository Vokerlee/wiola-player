/**
 * @file
 * @brief Local-file media provider composed from decoder and metadata adapters.
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

#include <codec/provider.hpp>

#include <codec/decode/open.hpp>
#include <codec/tags/tags.hpp>
#include <media/local_file_stream.hpp>

#include <utility>

namespace wiola::codec {

namespace {

class LocalMediaAsset final : public media::MediaAsset {
public:
    explicit LocalMediaAsset(std::filesystem::path path)
        : path_{std::move(path)}
    {
    }

    media::Result<media::MediaInfo> inspect() override
    {
        auto decoder{open_file(path_)};

        if (!decoder)
            return media::unexpected{decoder.error()};

        const pcm::StreamSpec spec{(*decoder)->spec()};
        return media::MediaInfo{.spec = spec,
            .duration = spec.valid() ? std::optional{spec.time_per((*decoder)->num_frames())}
                                     : std::nullopt};
    }

    media::Result<media::Metadata> read_metadata() override { return read_tags(path_); }

    media::Result<std::unique_ptr<Decoder>> create_decoder() override { return open_file(path_); }

private:
    std::filesystem::path path_;
};

} // namespace

media::Result<std::unique_ptr<media::MediaAsset>>
LocalMediaProvider::open(const std::filesystem::path& path)
{
    auto stream{media::LocalFileStream::open(path)};

    if (!stream)
        return media::unexpected{stream.error()};

    std::unique_ptr<media::MediaAsset> asset{std::make_unique<LocalMediaAsset>(path)};
    return asset;
}

} // namespace wiola::codec
