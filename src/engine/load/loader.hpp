/**
 * @file
 * @brief Opens identified load requests on a worker.
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

#pragma once

#include <codec/decode/open.hpp>
#include <core/macros.hpp>
#include <engine/load/load_request.hpp>

#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace wiola::engine {

using DecoderOpener = std::function<codec::Opened(const std::filesystem::path&)>;

/// Latest-request-wins decoder opening with request identity retained in every completion.
class Loader {
public:
    Loader();
    explicit Loader(DecoderOpener open);

    NO_COPY_SEMANTIC(Loader);
    NO_MOVE_SEMANTIC(Loader);

    ~Loader();

    /// Replaces any queued request; an in-progress third-party open finishes physically.
    void start(LoadRequest request);
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] std::optional<LoadCompletion> take();

private:
    void work(const std::stop_token& stop);

    DecoderOpener open_;
    mutable std::mutex mutex_;
    std::condition_variable_any waiting_;
    std::optional<LoadRequest> asked_;
    std::optional<LoadCompletion> done_;
    std::size_t num_asked_{0};
    bool opening_{false};
    std::jthread thread_;
};

} // namespace wiola::engine
