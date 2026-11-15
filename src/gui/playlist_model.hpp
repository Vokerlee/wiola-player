/**
 * @file
 * @brief Incremental Qt model over immutable application playlist row values.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
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

#include <engine/queue/playlist.hpp>

#include <QAbstractTableModel>

#include <optional>
#include <vector>

namespace wiola::gui {

class PlaylistModel final : public QAbstractTableModel {
public:
    explicit PlaylistModel(const QWidget& styled, ArtworkCachePolicy policy = {});

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void show_playlist(const engine::Playlist& playlist);

private:
    mutable ArtworkCache artwork_;
    ArtworkCachePolicy policy_;
    std::vector<engine::Track> rows_;
    std::optional<std::size_t> current_;
    std::optional<std::size_t> structural_revision_;
    std::optional<std::size_t> metadata_revision_;
    std::optional<std::size_t> selection_revision_;
};

} // namespace wiola::gui
