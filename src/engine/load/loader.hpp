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

#pragma once

#include <core/macros.hpp>
#include <engine/load/load_request.hpp>
#include <wiola/media/audio_decoder.hpp>
#include <wiola/media/provider.hpp>

#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace wiola::engine {

/// Opening receives the active cancellation/deadline context. Implementations honor it before
/// expensive work and during blocking operations; stale completions are not installed.
using DecoderOpener = std::function<media::DecoderOpenResult(const std::filesystem::path&,
    const media::OperationContext&)>;

/// Latest-request-wins decoder opening with request identity retained in every completion.
class Loader {
public:
    /// The provider or opener is selected by the host, never by the worker.
    explicit Loader(std::shared_ptr<media::MediaProvider> provider);
    explicit Loader(DecoderOpener open);

    NO_COPY_SEMANTIC(Loader);
    NO_MOVE_SEMANTIC(Loader);

    ~Loader();

    /// Replaces any queued request and cooperatively cancels only an active open. A completed
    /// decoder keeps its operation context alive after a later request starts.
    void start(LoadRequest request);
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] std::optional<LoadCompletion> take();

private:
    void work(const std::stop_token& stop);

    DecoderOpener open_;
    std::stop_source active_stop_;
    mutable std::mutex mutex_;
    std::condition_variable_any waiting_;
    std::optional<LoadRequest> asked_;
    std::optional<LoadCompletion> done_;
    std::size_t num_asked_{0};
    bool opening_{false};
    std::jthread thread_;
};

} // namespace wiola::engine
