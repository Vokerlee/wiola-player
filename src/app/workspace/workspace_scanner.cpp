/**
 * @file
 * @brief Cooperative worker-thread audio enumeration for folder workspaces.
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

#include "workspace_scanner.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace wiola::app {

namespace {

bool supported(const std::filesystem::path& path, const std::vector<std::string>& extensions)
{
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return std::ranges::find(extensions, extension) != extensions.end();
}

} // namespace

WorkspaceScanner::WorkspaceScanner(WorkspaceScanPolicy policy)
    : policy_{std::move(policy)}
{
    if (policy_.batch_size == 0 || policy_.batch_size > 1024 || policy_.max_tracks == 0 ||
        policy_.max_entries == 0 || policy_.max_directories == 0)
        throw std::invalid_argument{"invalid workspace scan limits"};
}

WorkspaceScanner::~WorkspaceScanner()
{
    stop();
}

void WorkspaceScanner::stop()
{
    stopped_permanently_ = true;
    cancel();
    if (worker_.joinable())
        worker_.join();
}

std::uint64_t WorkspaceScanner::start(const library::WorkspaceRecord& workspace,
    std::vector<std::string> supported_extensions, Observer observer,
    std::vector<library::TrackRecord> known)
{
    if (stopped_permanently_)
        return 0;
    cancel();

    if (worker_.joinable())
        worker_.join();

    stopped_ = {};
    const std::uint64_t generation = ++generation_;
    const std::stop_token stopped = stopped_.get_token();
    worker_ = std::jthread{
        [this, workspace, extensions = std::move(supported_extensions),
            observer = std::move(observer), generation, stopped, known = std::move(known)] {
            try {
                scan(stopped, workspace, std::move(extensions), observer, generation, known);
            } catch (const std::runtime_error&) {
                observer({.workspace = workspace.id,
                    .generation = generation,
                    .entries = {},
                    .status = WorkspaceScanStatus::failed,
                    .error = WorkspaceScanError::io,
                    .directories = {}});
            }
        }};

    return generation;
}

void WorkspaceScanner::cancel()
{
    stopped_.request_stop();
}

void WorkspaceScanner::scan(std::stop_token stopped, const library::WorkspaceRecord& workspace,
    std::vector<std::string> supported_extensions, const Observer& observer,
    std::uint64_t generation, const std::vector<library::TrackRecord>& known) const
{
    std::error_code error;
    if (!std::filesystem::is_directory(workspace.root, error) || error) {
        observer({.workspace = workspace.id,
            .generation = generation,
            .entries = {},
            .status = WorkspaceScanStatus::failed,
            .error = WorkspaceScanError::unreadable_root,
            .directories = {}});
        return;
    }
    for (auto& extension : supported_extensions)
        std::ranges::transform(extension, extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    WorkspaceDiscovery discovery{workspace.root, known, stopped};
    WorkspaceScanBatch batch{.workspace = workspace.id,
        .generation = generation,
        .entries = {},
        .status = WorkspaceScanStatus::running,
        .error = WorkspaceScanError::none,
        .directories = {workspace.root}};
    std::size_t visited{}, tracks{}, directories{1};
    const auto flush = [&](WorkspaceScanStatus status,
                           WorkspaceScanError failure = WorkspaceScanError::none) {
        batch.status = status;
        batch.error = failure;
        observer(std::move(batch));
        batch = {.workspace = workspace.id,
            .generation = generation,
            .entries = {},
            .status = WorkspaceScanStatus::running,
            .error = WorkspaceScanError::none,
            .directories = {}};
    };
    WorkspaceScanError failure = WorkspaceScanError::none;
    const auto accept = [&](const std::filesystem::directory_entry& entry) {
        if (stopped.stop_requested())
            return false;
        if (++visited > policy_.max_entries) {
            failure = WorkspaceScanError::limit_exceeded;
            return false;
        }
        std::error_code type_error;
        if (entry.is_directory(type_error) && workspace.recursive) {
            if (++directories > policy_.max_directories) {
                failure = WorkspaceScanError::limit_exceeded;
                return false;
            }
            batch.directories.push_back(entry.path().lexically_normal());
        } else if (!type_error && entry.is_regular_file(type_error) &&
            supported(entry.path(), supported_extensions)) {
            if (++tracks > policy_.max_tracks) {
                failure = WorkspaceScanError::limit_exceeded;
                return false;
            }
            batch.entries.push_back(discovery.resolve(entry.path().lexically_normal()));
        }
        if (type_error) {
            failure = WorkspaceScanError::io;
            return false;
        }
        if (batch.entries.size() >= policy_.batch_size ||
            batch.directories.size() >= policy_.batch_size)
            flush(WorkspaceScanStatus::running);
        if (policy_.after_entry)
            policy_.after_entry(entry.path(), visited, stopped);
        return !stopped.stop_requested();
    };
    if (workspace.recursive) {
        std::filesystem::recursive_directory_iterator current{workspace.root, error}, end;
        while (!error && current != end) {
            if (!accept(*current))
                break;
            current.increment(error);
        }
    } else {
        std::filesystem::directory_iterator current{workspace.root, error}, end;
        while (!error && current != end) {
            if (!accept(*current))
                break;
            current.increment(error);
        }
    }
    if (stopped.stop_requested())
        flush(WorkspaceScanStatus::cancelled);
    else if (error || failure != WorkspaceScanError::none)
        flush(WorkspaceScanStatus::failed,
            error == std::errc::permission_denied     ? WorkspaceScanError::permission_denied
                : failure == WorkspaceScanError::none ? WorkspaceScanError::io
                                                      : failure);
    else
        flush(WorkspaceScanStatus::complete);
}

} // namespace wiola::app
