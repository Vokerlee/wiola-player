/**
 * @file
 * @brief Incrementally populated folder workspace document.
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

#include "track_table_view.hpp"

#include <app/workspace_scan_publisher.hpp>

#include <engine/queue/playlist.hpp>
#include <library/repository.hpp>

#include <QCheckBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QWidget>

namespace wiola::gui {

class TrackSearchPanel;
class TagFilterPanel;

class WorkspaceDocument final : public QWidget {
public:
    explicit WorkspaceDocument(QWidget* parent = nullptr);

    void begin(const library::WorkspaceRecord& workspace, std::uint64_t generation);
    bool show_batch(const app::WorkspaceScanBatch& batch);
    bool show_media_revision(const library::LibraryRepository& repository,
        const library::MediaId& media);
    void show_library(const library::LibraryRepository& repository);
    void show_cancelled();
    [[nodiscard]] std::vector<engine::QueueEntryId> chosen_tracks() const;
    [[nodiscard]] std::vector<std::filesystem::path> chosen_paths() const;
    [[nodiscard]] std::vector<std::filesystem::path> displayed_paths() const;
    void on_apply_order_requested(std::function<void()> observer);
    void on_rescan_requested(std::function<void(bool recursive)> observer);
    void on_cancel_requested(std::function<void()> observer);
    void on_play_requested(std::function<void()> observer);
    void on_append_requested(std::function<void()> observer);
    void on_transfer_active_requested(std::function<void()> observer);
    void on_transfer_destination_requested(std::function<void()> observer);

private:
    void set_busy(bool busy);
    void update_results();

    library::WorkspaceId workspace_;
    std::uint64_t generation_{0};
    engine::Playlist rows_;
    QLabel* heading_{nullptr};
    QLabel* status_{nullptr};
    QCheckBox* recursive_{nullptr};
    QPushButton* rescan_{nullptr};
    QPushButton* cancel_{nullptr};
    QPushButton* play_{nullptr};
    QPushButton* append_{nullptr};
    QProgressBar* progress_{nullptr};
    TrackTableView* table_{nullptr};
    TrackSearchPanel* search_{nullptr};
    TagFilterPanel* filters_{nullptr};
    std::function<void(bool)> rescan_observer_;
    std::function<void()> cancel_observer_;
    std::function<void()> play_observer_;
    std::function<void()> append_observer_;
    std::function<void()> transfer_active_observer_;
    std::function<void()> transfer_destination_observer_;
    std::function<void()> apply_order_observer_;
};

} // namespace wiola::gui
