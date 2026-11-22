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

#include "workspace_watcher.hpp"

#include <platform/qt/qt_path_adapter.hpp>

#include <QDir>

#include <algorithm>
#include <chrono>

namespace wiola::app {

namespace {

constexpr std::chrono::milliseconds debounce_time{200};
constexpr std::size_t max_watched_directories{4096};

} // namespace

WorkspaceWatcher::WorkspaceWatcher(QObject* parent)
    : QObject{parent}
{
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, &WorkspaceWatcher::changed);
}

bool WorkspaceWatcher::watch(const library::WorkspaceRecord& workspace)
{
    if (!workspace.id.valid() || workspace.root.empty() ||
        !std::filesystem::is_directory(workspace.root))
        return false;

    unwatch(workspace.id);

    QStringList paths{platform::qt::QtPathAdapter::to_qt(workspace.root)};

    if (workspace.recursive) {
        std::error_code error;
        std::filesystem::recursive_directory_iterator current{workspace.root,
            std::filesystem::directory_options::skip_permission_denied, error};
        const std::filesystem::recursive_directory_iterator end;

        while (!error && current != end &&
            static_cast<std::size_t>(paths.size()) < max_watched_directories) {
            if (current->is_directory(error) && !error)
                paths.push_back(platform::qt::QtPathAdapter::to_qt(current->path()));
            current.increment(error);
        }
    }

    watched_.emplace(workspace.id, paths);
    watcher_.addPaths(paths);

    return true;
}

void WorkspaceWatcher::unwatch(const library::WorkspaceId& id)
{
    if (const auto found = watched_.find(id); found != watched_.end()) {
        watcher_.removePaths(found->second);
        watched_.erase(found);
    }

    if (QTimer* timer = pending_.contains(id) ? pending_.at(id) : nullptr) {
        timer->stop();
        timer->deleteLater();
        pending_.erase(id);
    }
}

void WorkspaceWatcher::on_changed(std::function<void(library::WorkspaceId)> observer)
{
    observer_ = std::move(observer);
}

void WorkspaceWatcher::changed(const QString& path)
{
    const QString changed_path = QDir::cleanPath(path);

    for (const auto& [id, paths] : watched_) {
        if (std::ranges::any_of(paths, [&changed_path](const QString& watched) {
                return QDir::cleanPath(watched) == changed_path;
            })) {
            debounce(id);
            return;
        }
    }
}

void WorkspaceWatcher::debounce(const library::WorkspaceId& id)
{
    if (QTimer* timer = pending_.contains(id) ? pending_.at(id) : nullptr) {
        timer->start();
        return;
    }

    QTimer* timer = new QTimer{this};
    timer->setSingleShot(true);
    timer->setInterval(debounce_time);
    pending_.emplace(id, timer);

    connect(timer, &QTimer::timeout, this, [this, id, timer] {
        pending_.erase(id);
        timer->deleteLater();

        if (observer_ && watched_.contains(id))
            observer_(id);
    });

    timer->start();
}

} // namespace wiola::app
