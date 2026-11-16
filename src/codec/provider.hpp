/**
 * @file
 * @brief Local-file media provider composed from decoder and metadata adapters.
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

#include <codec/decode/registry.hpp>
#include <media/provider.hpp>

namespace wiola::codec {

class LocalMediaProvider final : public media::MediaProvider {
public:
    LocalMediaProvider();
    explicit LocalMediaProvider(MediaBackendRegistry registry);

    media::Result<std::unique_ptr<media::MediaAsset>>
    open(const std::filesystem::path& path) override;

    [[nodiscard]] std::vector<std::string> playback_extensions() const override;

private:
    MediaBackendRegistry registry_;
};

} // namespace wiola::codec
