/**
 * @file
 * @brief Identity-scoped background metadata writes with coalescing and conflict checks.
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

#include <library/in_memory_repository.hpp>
#include <media/provider.hpp>

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace wiola::app {

struct BackgroundMetadataWriteResult {
    library::MediaId media;
    std::filesystem::path location;
    media::MetadataWriteOutcome outcome;
};

class BackgroundMetadataWrites final {
public:
    using Observer = std::function<void(BackgroundMetadataWriteResult)>;

    BackgroundMetadataWrites(std::shared_ptr<media::MediaProvider> provider,
        library::InMemoryRepository& repository);
    ~BackgroundMetadataWrites();

    [[nodiscard]] bool enqueue(const library::MediaId& media, std::filesystem::path path,
        media::MetadataWriteRequest request, Observer observer);

private:
    struct Job {
        library::MediaId media;
        std::filesystem::path path;
        media::MetadataWriteRequest request;
        Observer observer;
    };

    void run();
    [[nodiscard]] bool conflicts(const Job& job) const;

    std::shared_ptr<media::MediaProvider> provider_;
    library::InMemoryRepository& repository_;
    std::mutex mutex_;
    std::condition_variable available_;
    std::deque<std::shared_ptr<Job>> jobs_;
    std::map<library::MediaId, std::shared_ptr<Job>> pending_;
    bool stopping_{false};
    std::jthread worker_;
};

} // namespace wiola::app
