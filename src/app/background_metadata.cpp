/**
 * @file
 * @brief Bounded worker queue for reading track metadata.
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

#include "background_metadata.hpp"

#include <utility>

namespace wiola::app {

namespace {

constexpr std::size_t max_pending_metadata{256};

} // namespace

BackgroundMetadata::BackgroundMetadata(std::shared_ptr<media::MediaProvider> provider)
    : provider_{std::move(provider)}
    , worker_{[this] { run(); }}
{
}

BackgroundMetadata::~BackgroundMetadata()
{
    {
        std::lock_guard lock{mutex_};
        stopping_ = true;
        jobs_.clear();
    }

    available_.notify_one();
}

bool BackgroundMetadata::enqueue(const library::MediaId& media, std::filesystem::path path,
    Observer observer)
{
    if (!media.valid())
        return false;

    {
        std::lock_guard lock{mutex_};
        if (stopping_ || jobs_.size() >= max_pending_metadata)
            return false;

        jobs_.push_back({.media = media, .path = std::move(path), .observer = std::move(observer)});
    }

    available_.notify_one();
    return true;
}

void BackgroundMetadata::run()
{
    while (true) {
        Job job;

        {
            std::unique_lock lock{mutex_};
            available_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });

            if (stopping_)
                return;

            job = std::move(jobs_.front());
            jobs_.pop_front();
        }

        BackgroundMetadataResult result{.media = job.media, .metadata = {}, .error = {}};
        media::Result<std::unique_ptr<media::MediaAsset>> asset = provider_->open(job.path);

        if (!asset) {
            result.error = asset.error();
        } else {
            media::Result<media::Metadata> metadata = (*asset)->read_metadata();

            if (metadata)
                result.metadata = std::move(metadata).value();
            else
                result.error = metadata.error();
        }

        job.observer(std::move(result));
    }
}

} // namespace wiola::app
