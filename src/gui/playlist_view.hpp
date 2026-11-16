/**
 * @file
 * @brief The queue, as a listener sees it.
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

#include "playlist_model.hpp"

#include <QKeyEvent>
#include <QTableView>

#include <cstddef>
#include <vector>

namespace wiola::gui {

class PlaylistView final : public QTableView {
    Q_OBJECT

public:
    explicit PlaylistView(QWidget* parent = nullptr);
    void show_playlist(const engine::Playlist& playlist);
    [[nodiscard]] std::vector<std::size_t> chosen_rows() const;

signals:
    void track_chosen(std::size_t index);
    void removal_asked();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    PlaylistModel model_;
};

} // namespace wiola::gui
