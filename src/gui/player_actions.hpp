/**
 * @file
 * @brief Shared actions for every existing player command.
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

#include <engine/queue/playlist.hpp>

#include <QAction>
#include <QIcon>
#include <QObject>

#include <array>
#include <cstddef>

class QWidget;

namespace wiola::gui {

/// Stable command identities used independently of any particular button or menu.
enum class PlayerAction : std::size_t {
    open,
    previous,
    play,
    next,
    stop,
    repeat,
    shuffle,
    add,
    remove,
    clear,
    equalizer,
    count,
};

class PlayerActions final : public QObject {
    Q_OBJECT

public:
    explicit PlayerActions(QWidget& styled);

    [[nodiscard]] QAction* action(PlayerAction id) const noexcept;

    void set_available(PlayerAction id, bool available);
    void set_playing(bool playing);
    void set_repeat(engine::Playlist::Repeat repeat);
    void set_shuffled(bool shuffled);

private:
    static constexpr std::size_t index_of(PlayerAction id) noexcept
    {
        return static_cast<std::size_t>(id);
    }

    std::array<QAction*, static_cast<std::size_t>(PlayerAction::count)> actions_{};
    QIcon play_icon_;
    QIcon pause_icon_;
};

} // namespace wiola::gui
