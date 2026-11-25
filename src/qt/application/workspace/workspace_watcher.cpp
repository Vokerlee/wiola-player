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
#include <QFileSystemWatcher>
#include <QThread>
#include <algorithm>
#include <app/workspace/workspace_discovery.hpp>
#include <library/location.hpp>
#include <qt/paths.hpp>
#include <stdexcept>

namespace wiola::app {
namespace {

class QtWorkspaceWatchBackend final : public WorkspaceWatchBackend {
public:
    QtWorkspaceWatchBackend()
    {
        QObject::connect(&watcher_, &QFileSystemWatcher::directoryChanged, &watcher_,
            [this](const QString& path) {
                if (observer_)
                    observer_(platform::qt::QtPathAdapter::from_qt(path));
            });
    }

    bool add(const std::filesystem::path& path) override
    {
        return watcher_.addPath(platform::qt::QtPathAdapter::to_qt(path));
    }

    void remove(const std::filesystem::path& path) override
    {
        if (contains(path))
            watcher_.removePath(platform::qt::QtPathAdapter::to_qt(path));
    }

    bool contains(const std::filesystem::path& path) const override
    {
        return watcher_.directories().contains(platform::qt::QtPathAdapter::to_qt(path));
    }

    void on_changed(Observer observer) override { observer_ = std::move(observer); }

private:
    Observer observer_;
    QFileSystemWatcher watcher_;
};

std::optional<std::filesystem::path> normalized(const std::filesystem::path& path)
{
    std::error_code error;
    auto result = std::filesystem::absolute(path, error).lexically_normal();
    if (error)
        return {};
    while (result != result.root_path() && result.filename().empty())
        result = result.parent_path();
    return result;
}

} // namespace

WorkspaceWatcher::WorkspaceWatcher(QObject* parent)
    : WorkspaceWatcher{std::make_unique<QtWorkspaceWatchBackend>(), {}, parent}
{
}

WorkspaceWatcher::WorkspaceWatcher(std::unique_ptr<WorkspaceWatchBackend> backend,
    WorkspaceWatchPolicy policy, QObject* parent)
    : QObject{parent}
    , policy_{policy}
    , backend_{std::move(backend)}
{
    if (!backend_ || policy_.max_paths == 0 || policy_.debounce.count() < 0)
        throw std::invalid_argument{"invalid workspace watch policy"};
    backend_->on_changed([this](auto path) { changed(path); });
}

bool WorkspaceWatcher::add(const library::WorkspaceId& id, const std::filesystem::path& path)
{
    auto registration = watched_.find(id);
    const auto normal = normalized(path);
    if (registration == watched_.end() || !normal)
        return false;
    const auto key = library::media_id_for(*normal).value;
    registration->second.keys.insert(key);
    if (!paths_.contains(key) && installed_count_ >= policy_.max_paths) {
        registration->second.refused.insert_or_assign(key, *normal);
        return false;
    }
    auto [position, created] = paths_.try_emplace(key, Subscription{*normal, {}, false});
    auto& subscription = position->second;
    subscription.interested.insert(id);
    if (subscription.installed && !backend_->contains(subscription.path)) {
        subscription.installed = false;
        --installed_count_;
    }
    if (!subscription.installed) {
        if (installed_count_ >= policy_.max_paths) {
            registration->second.refused.insert_or_assign(key, *normal);
            return false;
        }
        subscription.installed = backend_->add(subscription.path);
        if (subscription.installed)
            ++installed_count_;
    }
    if (subscription.installed) {
        const auto interested = subscription.interested;
        for (const auto& workspace : interested)
            if (const auto found = watched_.find(workspace); found != watched_.end())
                found->second.refused.erase(key);
    } else
        registration->second.refused.insert_or_assign(key, *normal);
    return subscription.installed;
}

void WorkspaceWatcher::remove(const library::WorkspaceId& id, const std::string& key)
{
    const auto found = paths_.find(key);
    if (found == paths_.end())
        return;
    found->second.interested.erase(id);
    if (found->second.interested.empty()) {
        if (found->second.installed) {
            backend_->remove(found->second.path);
            --installed_count_;
        }
        paths_.erase(found);
    }
}

void WorkspaceWatcher::fallback_parent(const library::WorkspaceId& id)
{
    const auto found = watched_.find(id);
    if (found == watched_.end())
        return;
    auto parent = found->second.workspace.root.parent_path();
    for (std::size_t depth = 0; depth < 256 && !parent.empty(); ++depth) {
        std::error_code error;
        if (std::filesystem::is_directory(parent, error) && !error) {
            const auto key = library::media_id_for(parent).value;
            found->second.base.insert(key);
            static_cast<void>(add(id, parent));
            return;
        }
        const auto next = parent.parent_path();
        if (next == parent)
            return;
        parent = next;
    }
}

bool WorkspaceWatcher::watch(const library::WorkspaceRecord& workspace)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (stopped_ || !workspace.id.valid() || workspace.root.empty())
        return false;
    const auto root = normalized(workspace.root);
    if (!root)
        return false;
    unwatch(workspace.id);
    Registration registration;
    registration.workspace = workspace;
    registration.workspace.root = *root;
    registration.epoch = ++next_epoch_;
    const auto root_key = library::media_id_for(*root).value;
    registration.base.insert(root_key);
    registration.awaiting = workspace.recursive;
    std::error_code error;
    registration.root_missing =
        !std::filesystem::is_directory(*root, error) || static_cast<bool>(error);
    watched_.emplace(workspace.id, std::move(registration));
    const bool installed = add(workspace.id, *root);
    if (!installed)
        fallback_parent(workspace.id);
    const bool available = installed && !watched_.at(workspace.id).root_missing;
    report(workspace.id);
    return available;
}

void WorkspaceWatcher::scan_started(const library::WorkspaceId& id)
{
    if (const auto found = watched_.find(id); found != watched_.end()) {
        found->second.discovered = found->second.base;
        found->second.awaiting = found->second.workspace.recursive;
        found->second.failed = false;
    }
}

void WorkspaceWatcher::directories_discovered(const library::WorkspaceId& id,
    const std::vector<std::filesystem::path>& directories, bool finished, bool successful)
{
    const auto found = watched_.find(id);
    if (stopped_ || found == watched_.end())
        return;
    for (const auto& path : directories) {
        const auto normal = normalized(path);
        if (!normal || !workspace_contains(found->second.workspace.root, *normal))
            continue;
        const auto key = library::media_id_for(*normal).value;
        found->second.discovered.insert(key);
        static_cast<void>(add(id, *normal));
        if (key == library::media_id_for(found->second.workspace.root).value)
            found->second.root_missing = false;
    }
    if (finished) {
        found->second.awaiting = false;
        found->second.failed = !successful;
        if (successful) {
            const auto root_key = library::media_id_for(found->second.workspace.root).value;
            if (!found->second.root_missing) {
                for (const auto& base : found->second.base)
                    if (base != root_key)
                        found->second.discovered.erase(base);
                found->second.base = {root_key};
            }
            const auto old = found->second.keys;
            for (const auto& key : old)
                if (!found->second.discovered.contains(key)) {
                    remove(id, key);
                    found->second.keys.erase(key);
                    found->second.refused.erase(key);
                }
            const auto retry = found->second.refused;
            for (const auto& [key, path] : retry)
                if (found->second.discovered.contains(key))
                    static_cast<void>(add(id, path));
        }
    }
    report(id);
}

std::optional<WorkspaceWatchCoverage>
WorkspaceWatcher::coverage(const library::WorkspaceId& id) const
{
    const auto found = watched_.find(id);
    if (found == watched_.end())
        return {};
    const auto& state = found->second;
    return WorkspaceWatchCoverage{id,
        state.root_missing || state.awaiting || state.failed || !state.refused.empty(),
        state.root_missing, state.awaiting, state.refused.size()};
}

void WorkspaceWatcher::report(const library::WorkspaceId& id)
{
    const auto value = coverage(id);
    if (value && status_)
        status_(*value);
}

void WorkspaceWatcher::stop()
{
    stopped_ = true;
    backend_->on_changed({});
    while (!watched_.empty())
        unwatch(watched_.begin()->first);
    observer_ = {};
    status_ = {};
}

void WorkspaceWatcher::unwatch(library::WorkspaceId id)
{
    if (const auto found = watched_.find(id); found != watched_.end()) {
        const auto keys = std::move(found->second.keys);
        watched_.erase(found);
        for (const auto& key : keys)
            remove(id, key);
    }
    if (const auto timer = pending_.find(id); timer != pending_.end()) {
        timer->second->stop();
        timer->second->deleteLater();
        pending_.erase(timer);
    }
}

void WorkspaceWatcher::on_changed(std::function<void(library::WorkspaceId)> observer)
{
    observer_ = std::move(observer);
}

void WorkspaceWatcher::changed(const std::filesystem::path& path)
{
    if (stopped_)
        return;
    const auto normal = normalized(path);
    if (!normal)
        return;
    const auto key = library::media_id_for(*normal).value;
    const auto found = paths_.find(key);
    if (found == paths_.end())
        return;
    const bool installed = backend_->contains(found->second.path);
    if (found->second.installed && !installed)
        --installed_count_;
    if (!found->second.installed && installed)
        ++installed_count_;
    found->second.installed = installed;
    const auto interested = found->second.interested;
    for (const auto& id : interested) {
        const auto registration = watched_.find(id);
        if (registration == watched_.end())
            continue;
        std::error_code error;
        const auto root = registration->second.workspace.root;
        registration->second.root_missing =
            !std::filesystem::is_directory(root, error) || static_cast<bool>(error);
        if (registration->second.root_missing) {
            registration->second.refused.insert_or_assign(library::media_id_for(root).value, root);
            fallback_parent(id);
        } else
            static_cast<void>(add(id, root));
        debounce(id);
        report(id);
    }
}

void WorkspaceWatcher::debounce(const library::WorkspaceId& id)
{
    const auto registration = watched_.find(id);
    if (registration == watched_.end())
        return;
    if (const auto found = pending_.find(id); found != pending_.end()) {
        found->second->start();
        return;
    }
    auto* timer = new QTimer{this};
    timer->setSingleShot(true);
    timer->setInterval(policy_.debounce);
    pending_.emplace(id, timer);
    connect(timer, &QTimer::timeout, this, [this, id, timer, epoch = registration->second.epoch] {
        const auto pending = pending_.find(id);
        const auto registration = watched_.find(id);
        if (pending == pending_.end() || pending->second != timer ||
            registration == watched_.end() || registration->second.epoch != epoch) {
            timer->deleteLater();
            return;
        }
        pending_.erase(pending);
        timer->deleteLater();
        if (!stopped_ && observer_)
            observer_(id);
    });
    timer->start();
}

} // namespace wiola::app
