/**
 * @file
 * @brief Captured metadata edit targets, expected state, mixed fields and draft.
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
#include "metadata_write_job.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <media/provider.hpp>
#include <mutex>
#include <thread>

namespace wiola::app {

/// One bounded staging worker. Jobs own immutable file expectations; no live repository access.
class BackgroundMetadataWrites final {
public:
    using Observer = std::function<void(MetadataWriteResult)>;
    explicit BackgroundMetadataWrites(std::shared_ptr<media::MediaProvider> provider);
    ~BackgroundMetadataWrites();
    void stop();
    [[nodiscard]] std::optional<MetadataWriteOperationId> enqueue(MetadataWriteJob job,
        Observer observer, Observer progress = {});

private:
    struct Pending {
        MetadataWriteJob job;
        Observer observer;
        Observer progress;
    };

    void run();
    static void cancel(Pending pending);
    std::shared_ptr<media::MediaProvider> provider_;
    std::mutex mutex_;
    std::condition_variable available_;
    std::deque<Pending> jobs_;
    std::shared_ptr<std::stop_source> active_;
    bool stopping_{};
    std::uint64_t next_operation_{};
    std::jthread worker_;
};

} // namespace wiola::app
