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

#include <QCheckBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QWidget>

namespace wiola::gui {

class WorkspaceDocument final : public QWidget {
public:
    explicit WorkspaceDocument(QWidget* parent = nullptr);

    void begin(const library::WorkspaceRecord& workspace, std::uint64_t generation);
    bool show_batch(const app::WorkspaceScanBatch& batch);
    void show_cancelled();
    [[nodiscard]] std::vector<engine::QueueEntryId> chosen_tracks() const;
    void on_rescan_requested(std::function<void(bool recursive)> observer);
    void on_cancel_requested(std::function<void()> observer);

private:
    void set_busy(bool busy);

    library::WorkspaceId workspace_;
    std::uint64_t generation_{0};
    engine::Playlist rows_;
    QLabel* heading_{nullptr};
    QLabel* status_{nullptr};
    QCheckBox* recursive_{nullptr};
    QPushButton* rescan_{nullptr};
    QPushButton* cancel_{nullptr};
    QProgressBar* progress_{nullptr};
    TrackTableView* table_{nullptr};
    std::function<void(bool)> rescan_observer_;
    std::function<void()> cancel_observer_;
};

} // namespace wiola::gui
