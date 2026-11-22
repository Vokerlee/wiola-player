/**
 * @file
 * @brief Bounded, generation-aware background tag reading.
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

#include "tag_reader.hpp"
#include <media/artwork_reference.hpp>

#include <codec/provider.hpp>

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

namespace wiola::engine {

TagReader::TagReader()
    : TagReader{std::make_shared<codec::LocalMediaProvider>()}
{
}

TagReader::TagReader(std::shared_ptr<media::MediaProvider> provider, std::size_t max_pending,
    std::size_t max_completed)
    : TagReader{[provider = std::move(provider)](const std::filesystem::path& path,
                    const media::OperationContext& context) {
                    auto asset{provider->open(path, context)};
                    return asset ? (*asset)->read_metadata(context)
                                 : media::Result<media::Metadata>{media::unexpected{asset.error()}};
                },
          max_pending, max_completed}
{
}

TagReader::TagReader(MetadataReader read, std::size_t max_pending, std::size_t max_completed,
    std::size_t max_completed_artwork_bytes)
    : TagReader{[read = std::move(read)](const std::filesystem::path& path,
                    const media::OperationContext& context) {
                    if (context.stop_reason() != media::StopReason::none)
                        return media::Result<media::Metadata>{media::unexpected{context.error()}};

                    return read(path);
                },
          max_pending, max_completed, max_completed_artwork_bytes}
{
}

TagReader::TagReader(ContextMetadataReader read, std::size_t max_pending, std::size_t max_completed,
    std::size_t max_completed_artwork_bytes)
    : reader_{std::move(read)}
    , max_pending_{max_pending}
    , max_completed_{max_completed}
    , max_completed_artwork_bytes_{max_completed_artwork_bytes}
{
    if (max_pending_ == 0 || max_completed_ == 0 || max_completed_artwork_bytes_ == 0)
        throw std::invalid_argument{"metadata capacities must be positive"};
}

TagReader::~TagReader()
{
    if (thread_.joinable()) {
        active_stop_.request_stop();
        thread_.request_stop();
        waiting_.notify_all();
        collected_.notify_all();
        thread_.join();
    }
}

std::size_t TagReader::start(std::span<Request> requests)
{
    std::size_t accepted{0};

    {
        const std::lock_guard asked{mutex_};

        if (active_stop_.stop_requested())
            active_stop_ = std::stop_source{};

        const std::size_t available{max_pending_ - std::min(asked_.size(), max_pending_)};
        accepted = std::min(requests.size(), available);

        for (Request& request : requests.first(accepted))
            asked_.push_back(std::move(request));
    }

    if (accepted == 0)
        return 0;

    if (!thread_.joinable())
        thread_ = std::jthread{[this](const std::stop_token& stop) { work(stop); }};

    waiting_.notify_one();

    return accepted;
}

void TagReader::cancel_before(std::uint64_t generation)
{
    {
        const std::lock_guard asked{mutex_};
        active_stop_.request_stop();
        latest_generation_ = std::max(latest_generation_, generation);
        std::erase_if(asked_,
            [generation](const Request& request) { return request.generation < generation; });
        std::erase_if(read_,
            [generation](const Reading& reading) { return reading.generation < generation; });
        completed_artwork_bytes_ = 0;
        for (const Reading& reading : read_)
            completed_artwork_bytes_ += reading.result ? reading.result->artwork.data.size() : 0;
    }

    collected_.notify_all();
}

std::vector<TagReader::Reading> TagReader::take()
{
    std::vector<Reading> readings;
    {
        const std::lock_guard asked{mutex_};
        readings = std::exchange(read_, {});
        completed_artwork_bytes_ = 0;
    }
    collected_.notify_one();
    return readings;
}

bool TagReader::busy() const noexcept
{
    const std::lock_guard asked{mutex_};
    return !asked_.empty() || reading_;
}

void TagReader::work(const std::stop_token& stop)
{
    while (true) {
        Request request;
        media::OperationContext context;

        {
            std::unique_lock asked{mutex_};
            waiting_.wait(asked, stop, [this] { return !asked_.empty(); });

            if (stop.stop_requested())
                return;

            request = std::move(asked_.front());
            context.stop = active_stop_.get_token();
            asked_.pop_front();
            reading_ = true;
        }

        media::Result<media::Metadata> result{media::Metadata{}};

        try {
            result = reader_(request.path, context);
        } catch (const std::bad_alloc&) {
            // Allocation failure is fatal: no reliable error object or queue storage can be made.
            throw;
        } catch (...) {
            result = media::unexpected{
                media::Error{.category = media::ErrorCategory::unreadable,
                             .detail = "metadata reader failed"}
            };
        }

        constexpr std::size_t max_error_detail_bytes{1024};
        if (!result && result.error().detail.size() > max_error_detail_bytes)
            result.error().detail.resize(max_error_detail_bytes);

        if (result && result->artwork.data.size() > max_completed_artwork_bytes_)
            result->artwork = {};

        if (result)
            result->artwork.content_id = media::artwork_content_id(result->artwork);
        const std::size_t artwork_bytes{result ? result->artwork.data.size() : 0};

        {
            std::unique_lock asked{mutex_};

            if (request.generation < latest_generation_) {
                reading_ = false;
                continue;
            }

            collected_.wait(asked, stop, [this, artwork_bytes] {
                return read_.size() < max_completed_ &&
                    artwork_bytes <= max_completed_artwork_bytes_ - completed_artwork_bytes_;
            });

            if (stop.stop_requested())
                return;

            completed_artwork_bytes_ += artwork_bytes;
            read_.push_back(Reading{.generation = request.generation,
                .id = request.id,
                .path = std::move(request.path),
                .result = std::move(result)});
            reading_ = false;
        }
    }
}

} // namespace wiola::engine
