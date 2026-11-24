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
#include <wiola/media/artwork_reference.hpp>

#include <stdexcept>
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
    stop();
}

void BackgroundMetadata::stop()
{
    {
        std::lock_guard lock{mutex_};
        stopping_ = true;
        cancellation_.request_stop();
    }

    available_.notify_one();
    if (worker_.joinable())
        worker_.join();
}

bool BackgroundMetadata::enqueue(const library::MediaId& media, std::filesystem::path path,
    Observer observer)
{
    if (!media.valid() || !observer || path.empty())
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

            if (stopping_ && jobs_.empty())
                return;

            job = std::move(jobs_.front());
            jobs_.pop_front();
        }

        BackgroundMetadataResult result{.media = job.media,
            .location = job.path,
            .metadata = {},
            .error = {}};
        const media::OperationContext context{.stop = cancellation_.get_token(), .deadline = {}};
        try {
            if (context.stop_reason() != media::StopReason::none)
                result.error = context.error();
            else {
                auto asset = provider_->open(job.path, context);
                if (!asset)
                    result.error = asset.error();
                else {
                    auto metadata = (*asset)->read_metadata(context);
                    if (metadata)
                        result.metadata = std::move(metadata).value();
                    else
                        result.error = metadata.error();
                }
            }
        } catch (const std::runtime_error& error) {
            result.error =
                media::Error{.category = media::ErrorCategory::read_failed, .detail = error.what()};
        }
        if (context.stop_reason() != media::StopReason::none) {
            result.metadata.reset();
            result.error = context.error();
        }

        if (result.metadata &&
            result.metadata->artwork.data.size() > media::max_encoded_artwork_bytes)
            result.metadata->artwork = {};
        if (result.metadata)
            result.metadata->artwork.content_id =
                media::artwork_content_id(result.metadata->artwork);
        job.observer(std::move(result));
    }
}

} // namespace wiola::app
