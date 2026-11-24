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

#include <new>
#include <utility>

namespace wiola::engine {

Loader::Loader(std::shared_ptr<media::MediaProvider> provider)
    : open_{[provider = std::move(provider)](const std::filesystem::path& path,
                const media::OperationContext& context) {
        auto asset{provider->open(path, context)};
        return asset ? (*asset)->create_decoder(context)
                     : media::DecoderOpenResult{media::unexpected{asset.error()}};
    }}
{
}

Loader::Loader(DecoderOpener open)
    : open_{std::move(open)}
{
}

Loader::~Loader()
{
    cancel();
    if (thread_.joinable()) {
        thread_.request_stop();
        waiting_.notify_all();
        thread_.join();
    }
}

void Loader::start(LoadRequest request)
{
    // Launch before publishing state: a failed launch cannot leave a permanently busy request.
    if (!thread_.joinable())
        thread_ = std::jthread{[this](const std::stop_token& stop) { work(stop); }};

    std::stop_source obsolete{std::nostopstate};
    std::optional<LoadCompletion> retired;
    {
        const std::lock_guard asked{mutex_};
        if (opening_)
            obsolete = active_stop_;
        asked_ = std::move(request);
        retired = std::exchange(done_, std::nullopt);
        ++num_asked_;
    }

    // request_stop invokes arbitrary collaborator callbacks synchronously.
    obsolete.request_stop();
    waiting_.notify_one();
}

void Loader::cancel() noexcept
{
    std::stop_source obsolete{std::nostopstate};
    std::optional<LoadCompletion> retired;
    {
        const std::lock_guard asked{mutex_};
        if (opening_)
            obsolete = active_stop_;
        asked_.reset();
        retired = std::exchange(done_, std::nullopt);
        ++num_asked_;
    }
    obsolete.request_stop();
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
            active_stop_ = std::stop_source{};
            context.stop = active_stop_.get_token();
            asked_when = num_asked_;
            opening_ = true;
        }

        auto opened{[&]() -> media::DecoderOpenResult {
            try {
                return open_(request.path, context);
            } catch (const std::bad_alloc&) {
                throw;
            } catch (...) {
                return media::unexpected{
                    media::Error{.category = media::ErrorCategory::unreadable,
                                 .detail = "decoder opener failed"}
                };
            }
        }()};

        {
            const std::lock_guard asked{mutex_};
            opening_ = false;

            if (asked_when == num_asked_)
                done_ = LoadCompletion{.request = std::move(request), .opened = std::move(opened)};
        }
    }
}

} // namespace wiola::engine
