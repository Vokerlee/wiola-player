/**
 * @file
 * @brief Incremental Qt table model addressed by stable track identities.
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

#include "artwork_cache.hpp"

#include <engine/queue/track.hpp>

#include <QAbstractTableModel>

#include <optional>
#include <vector>

namespace wiola::gui {

class TrackTableModel final : public QAbstractTableModel {
public:
    explicit TrackTableModel(const QWidget& styled, ArtworkCachePolicy policy = {});

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void show_tracks(const std::vector<engine::Track>& tracks,
        std::optional<engine::QueueEntryId> current = {});
    [[nodiscard]] std::optional<engine::QueueEntryId> track_id(const QModelIndex& index) const;

private:
    mutable ArtworkCache artwork_;
    ArtworkCachePolicy policy_;
    std::vector<engine::Track> rows_;
    std::optional<engine::QueueEntryId> current_;
};

} // namespace wiola::gui
