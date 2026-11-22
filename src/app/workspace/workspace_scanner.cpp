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
#include <utility>

namespace wiola::app {

namespace {

constexpr std::size_t batch_size{64};
constexpr std::size_t max_workspace_tracks{100000};

bool supported(const std::filesystem::path& path, const std::vector<std::string>& extensions)
{
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return std::ranges::find(extensions, extension) != extensions.end();
}

void publish(const WorkspaceScanner::Observer& observer, const library::WorkspaceId& workspace,
    std::uint64_t generation, std::vector<std::filesystem::path> tracks, WorkspaceScanStatus status)
{
    observer({.workspace = workspace,
        .generation = generation,
        .tracks = std::move(tracks),
        .status = status});
}

} // namespace

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
    std::vector<std::string> supported_extensions, Observer observer)
{
    if (stopped_permanently_)
        return 0;
    cancel();

    if (worker_.joinable())
        worker_.join();

    stopped_ = {};
    const std::uint64_t generation = ++generation_;
    const std::stop_token stopped = stopped_.get_token();
    worker_ = std::jthread{[this, workspace, extensions = std::move(supported_extensions),
                               observer = std::move(observer), generation, stopped] {
        scan(stopped, workspace, std::move(extensions), observer, generation);
    }};

    return generation;
}

void WorkspaceScanner::cancel()
{
    stopped_.request_stop();
}

void WorkspaceScanner::scan(std::stop_token stopped, const library::WorkspaceRecord& workspace,
    std::vector<std::string> supported_extensions, const Observer& observer,
    std::uint64_t generation) const
{
    std::error_code error;
    if (!std::filesystem::is_directory(workspace.root, error) || error) {
        publish(observer, workspace.id, generation, {}, WorkspaceScanStatus::failed);
        return;
    }

    std::vector<std::filesystem::path> tracks;
    bool failed = false;

    for (std::string& extension : supported_extensions) {
        std::ranges::transform(extension, extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }

    const auto accept = [&tracks, &supported_extensions,
                            &failed](const std::filesystem::directory_entry& entry) {
        std::error_code type_error;
        if (!entry.is_regular_file(type_error) || type_error ||
            !supported(entry.path(), supported_extensions))
            return;

        if (tracks.size() >= max_workspace_tracks) {
            failed = true;
            return;
        }

        tracks.push_back(entry.path().lexically_normal());
    };

    if (workspace.recursive) {
        std::filesystem::recursive_directory_iterator current{workspace.root,
            std::filesystem::directory_options::skip_permission_denied, error};
        const std::filesystem::recursive_directory_iterator end;

        while (!error && current != end) {
            if (stopped.stop_requested())
                break;

            accept(*current);
            current.increment(error);
        }
    } else {
        std::filesystem::directory_iterator current{workspace.root, error};
        const std::filesystem::directory_iterator end;

        while (!error && current != end) {
            if (stopped.stop_requested())
                break;

            accept(*current);
            current.increment(error);
        }
    }

    if (error)
        failed = true;

    if (stopped.stop_requested()) {
        publish(observer, workspace.id, generation, {}, WorkspaceScanStatus::cancelled);
        return;
    }

    std::ranges::sort(tracks);

    for (std::size_t offset = 0; offset < tracks.size(); offset += batch_size) {
        if (stopped.stop_requested()) {
            publish(observer, workspace.id, generation, {}, WorkspaceScanStatus::cancelled);
            return;
        }

        const auto last = std::min(offset + batch_size, tracks.size());
        publish(observer, workspace.id, generation,
            std::vector<std::filesystem::path>{tracks.begin() + static_cast<std::ptrdiff_t>(offset),
                tracks.begin() + static_cast<std::ptrdiff_t>(last)},
            WorkspaceScanStatus::running);
    }

    publish(observer, workspace.id, generation, {},
        failed ? WorkspaceScanStatus::failed : WorkspaceScanStatus::complete);
}

} // namespace wiola::app
