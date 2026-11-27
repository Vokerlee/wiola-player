/**
 * @file
 * @brief Debounced native filesystem notifications for folder workspaces.
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

#include <wiola/library/records.hpp>

#include <QTimer>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <set>

namespace wiola::app {

class WorkspaceWatchBackend {
public:
    using Observer = std::function<void(std::filesystem::path)>;
    virtual ~WorkspaceWatchBackend() = default;
    virtual bool add(const std::filesystem::path& path) = 0;
    virtual void remove(const std::filesystem::path& path) = 0;
    [[nodiscard]] virtual bool contains(const std::filesystem::path& path) const = 0;
    virtual void on_changed(Observer observer) = 0;
};

struct WorkspaceWatchPolicy {
    std::chrono::milliseconds debounce{200};
    std::size_t max_paths{4096};
};

struct WorkspaceWatchCoverage {
    library::WorkspaceId workspace;
    bool partial{}, root_missing{}, awaiting_discovery{};
    std::size_t refused_paths{};
};

/// Native subscriptions are shared by normalized path; each interested workspace retains its own
/// debounce.
class WorkspaceWatcher final : public QObject {
public:
    explicit WorkspaceWatcher(QObject* parent = nullptr);
    WorkspaceWatcher(std::unique_ptr<WorkspaceWatchBackend> backend,
        WorkspaceWatchPolicy policy = {}, QObject* parent = nullptr);

    ~WorkspaceWatcher() override { stop(); }

    [[nodiscard]] bool watch(const library::WorkspaceRecord& workspace);
    void scan_started(const library::WorkspaceId& id);
    void directories_discovered(const library::WorkspaceId& id,
        const std::vector<std::filesystem::path>& directories, bool finished, bool successful);
    [[nodiscard]] std::optional<WorkspaceWatchCoverage>
    coverage(const library::WorkspaceId& id) const;
    void stop();
    void unwatch(library::WorkspaceId id);
    void on_changed(std::function<void(library::WorkspaceId)> observer);

    void on_status(std::function<void(WorkspaceWatchCoverage)> observer)
    {
        status_ = std::move(observer);
    }

private:
    struct Registration {
        library::WorkspaceRecord workspace;
        std::uint64_t epoch;
        std::set<std::string> keys, base, discovered;
        std::map<std::string, std::filesystem::path> refused;
        bool root_missing{}, awaiting{}, failed{};
    };

    struct Subscription {
        std::filesystem::path path;
        std::set<library::WorkspaceId> interested;
        bool installed{};
    };

    bool add(const library::WorkspaceId& id, const std::filesystem::path& path);
    void remove(const library::WorkspaceId& id, const std::string& key);
    void fallback_parent(const library::WorkspaceId& id);
    void changed(const std::filesystem::path& path);
    void debounce(const library::WorkspaceId& id);
    void report(const library::WorkspaceId& id);
    bool stopped_{};
    WorkspaceWatchPolicy policy_;
    std::unique_ptr<WorkspaceWatchBackend> backend_;
    std::map<library::WorkspaceId, Registration> watched_;
    std::map<std::string, Subscription> paths_;
    std::map<library::WorkspaceId, QTimer*> pending_;
    std::uint64_t next_epoch_{};
    std::size_t installed_count_{};
    std::function<void(library::WorkspaceId)> observer_;
    std::function<void(WorkspaceWatchCoverage)> status_;
};

} // namespace wiola::app
