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

#include <library/records.hpp>

#include <QFileSystemWatcher>
#include <QTimer>

#include <functional>
#include <map>

namespace wiola::app {

class WorkspaceWatcher final : public QObject {
public:
    explicit WorkspaceWatcher(QObject* parent = nullptr);

    [[nodiscard]] bool watch(const library::WorkspaceRecord& workspace);
    void stop();
    void unwatch(const library::WorkspaceId& id);
    void on_changed(std::function<void(library::WorkspaceId)> observer);

private:
    void changed(const QString& path);
    void debounce(const library::WorkspaceId& id);

    bool stopped_{false};
    QFileSystemWatcher watcher_;
    std::map<library::WorkspaceId, QStringList> watched_;
    std::map<library::WorkspaceId, QTimer*> pending_;
    std::function<void(library::WorkspaceId)> observer_;
};

} // namespace wiola::app
