/**
 * @file
 * @brief Opens identified load requests on a worker.
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

#include "loader.hpp"

#include <codec/provider.hpp>

#include <new>
#include <utility>

namespace wiola::engine {

Loader::Loader()
    : Loader{std::make_shared<codec::LocalMediaProvider>()}
{
}

Loader::Loader(std::shared_ptr<media::MediaProvider> provider)
    : open_{[provider = std::move(provider)](const std::filesystem::path& path,
                const media::OperationContext& context) {
        auto asset{provider->open(path, context)};
        return asset ? (*asset)->create_decoder(context)
                     : codec::Opened{media::unexpected{asset.error()}};
    }}
{
}

Loader::Loader(DecoderOpener open)
    : open_{[open = std::move(open)](const std::filesystem::path& path,
                const media::OperationContext& context) {
        if (context.stop_reason() != media::StopReason::none)
            return codec::Opened{media::unexpected{context.error()}};

        return open(path);
    }}
{
}

Loader::Loader(ContextDecoderOpener open)
    : open_{std::move(open)}
{
}

Loader::~Loader()
{
    if (thread_.joinable()) {
        active_stop_.request_stop();
        thread_.request_stop();
        waiting_.notify_all();
        thread_.join();
    }
}

void Loader::start(LoadRequest request)
{
    {
        const std::lock_guard asked{mutex_};

        if (opening_)
            active_stop_.request_stop();

        active_stop_ = std::stop_source{};
        asked_ = std::move(request);
        done_.reset();
        ++num_asked_;
    }

    if (!thread_.joinable())
        thread_ = std::jthread{[this](const std::stop_token& stop) { work(stop); }};

    waiting_.notify_one();
}

bool Loader::busy() const noexcept
{
    const std::lock_guard asked{mutex_};
    return asked_.has_value() || opening_;
}

std::optional<LoadCompletion> Loader::take()
{
    const std::lock_guard asked{mutex_};
    return std::exchange(done_, std::nullopt);
}

void Loader::work(const std::stop_token& stop)
{
    while (true) {
        LoadRequest request;
        media::OperationContext context;
        std::size_t asked_when{0};

        {
            std::unique_lock asked{mutex_};
            waiting_.wait(asked, stop, [this] { return asked_.has_value(); });

            if (stop.stop_requested())
                return;

            request = *std::exchange(asked_, std::nullopt);
            context.stop = active_stop_.get_token();
            asked_when = num_asked_;
            opening_ = true;
        }

        codec::Opened opened{
            media::unexpected{media::Error{.category = media::ErrorCategory::unreadable,
                .detail = "decoder was not opened"}}};

        try {
            opened = open_(request.path, context);
        } catch (const std::bad_alloc&) {
            throw;
        } catch (...) {
            opened = media::unexpected{
                media::Error{.category = media::ErrorCategory::unreadable,
                             .detail = "decoder opener failed"}
            };
        }

        {
            const std::lock_guard asked{mutex_};
            opening_ = false;

            if (asked_when == num_asked_)
                done_ = LoadCompletion{.request = std::move(request), .opened = std::move(opened)};
        }
    }
}

} // namespace wiola::engine
