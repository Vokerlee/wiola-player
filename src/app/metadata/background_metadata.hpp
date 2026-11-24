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

#pragma once

#include <library/identity.hpp>
#include <wiola/media/provider.hpp>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace wiola::app {

struct BackgroundMetadataResult {
    library::MediaId media;
    std::filesystem::path location;
    std::optional<media::Metadata> metadata;
    std::optional<media::Error> error;
};

class BackgroundMetadata final {
public:
    using Observer = std::function<void(BackgroundMetadataResult)>;

    explicit BackgroundMetadata(std::shared_ptr<media::MediaProvider> provider);
    ~BackgroundMetadata();
    /// Reject new reads, discard pending reads, and join the current read. Owner thread only.
    void stop();

    [[nodiscard]] bool enqueue(const library::MediaId& media, std::filesystem::path path,
        Observer observer);

private:
    struct Job {
        library::MediaId media;
        std::filesystem::path path;
        Observer observer;
    };

    void run();

    std::shared_ptr<media::MediaProvider> provider_;
    std::mutex mutex_;
    std::condition_variable available_;
    std::deque<Job> jobs_;
    bool stopping_{false};
    std::stop_source cancellation_;
    std::jthread worker_;
};

} // namespace wiola::app
