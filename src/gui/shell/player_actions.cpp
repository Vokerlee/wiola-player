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

#include "player_actions.hpp"

#include <gui/appearance/theme.hpp>

#include <QKeySequence>
#include <QSignalBlocker>
#include <QStyle>
#include <QWidget>

#include <optional>

namespace wiola::gui {

namespace {

struct ActionDescription {
    PlayerAction id;
    const char* text;
    QKeySequence shortcut;
    std::optional<QStyle::StandardPixmap> icon;
    bool checkable{false};
};

} // namespace

PlayerActions::PlayerActions(QWidget& styled)
    : QObject{}
{
    const std::array descriptions{
        ActionDescription{PlayerAction::open, "Open tracks", QKeySequence::Open,
                          QStyle::SP_DirOpenIcon},
        ActionDescription{PlayerAction::previous, "Previous track", QKeySequence{"Ctrl+Left"},
                          QStyle::SP_MediaSkipBackward},
        ActionDescription{PlayerAction::play, "Play", QKeySequence{"Space"}, QStyle::SP_MediaPlay},
        ActionDescription{PlayerAction::next, "Next track", QKeySequence{"Ctrl+Right"},
                          QStyle::SP_MediaSkipForward},
        ActionDescription{PlayerAction::stop, "Stop", QKeySequence{"Escape"}, QStyle::SP_MediaStop},
        ActionDescription{PlayerAction::repeat, "Repeat: off", QKeySequence{"Ctrl+R"},
                          std::nullopt},
        ActionDescription{PlayerAction::shuffle, "Shuffle", QKeySequence{"Ctrl+Shift+S"},
                          std::nullopt, true},
        ActionDescription{PlayerAction::add, "Add", QKeySequence{"Ctrl+Shift+A"}, std::nullopt},
        ActionDescription{PlayerAction::remove, "Remove", QKeySequence{"Delete"}, std::nullopt},
        ActionDescription{PlayerAction::clear, "Clear", QKeySequence{"Ctrl+Shift+Backspace"},
                          std::nullopt},
        ActionDescription{PlayerAction::equalizer, "EQ", QKeySequence{"Ctrl+E"}, std::nullopt},
    };

    for (const ActionDescription& description : descriptions) {
        auto* command = new QAction{description.text, this};

        command->setToolTip(description.text);
        command->setShortcut(description.shortcut);
        command->setCheckable(description.checkable);

        actions_[index_of(description.id)] = command;
    }

    reload_icons(styled);
}

void PlayerActions::reload_icons(const QWidget& styled)
{
    static constexpr std::array icons{
        std::pair{PlayerAction::open,     QStyle::SP_DirOpenIcon      },
        std::pair{PlayerAction::previous, QStyle::SP_MediaSkipBackward},
        std::pair{PlayerAction::play,     QStyle::SP_MediaPlay        },
        std::pair{PlayerAction::next,     QStyle::SP_MediaSkipForward },
        std::pair{PlayerAction::stop,     QStyle::SP_MediaStop        },
    };

    for (const auto& [id, icon] : icons)
        action(id)->setIcon(themed_standard_icon(styled, icon));

    play_icon_ = action(PlayerAction::play)->icon();
    pause_icon_ = themed_standard_icon(styled, QStyle::SP_MediaPause);
    set_playing(action(PlayerAction::play)->text() == "Pause");
}

QAction* PlayerActions::action(PlayerAction id) const noexcept
{
    return actions_[index_of(id)];
}

void PlayerActions::set_available(PlayerAction id, bool available)
{
    action(id)->setEnabled(available);
}

void PlayerActions::set_playing(bool playing)
{
    QAction* play = action(PlayerAction::play);
    const QString says{playing ? "Pause" : "Play"};

    play->setText(says);
    play->setToolTip(says);
    play->setIcon(playing ? pause_icon_ : play_icon_);
}

void PlayerActions::set_repeat(engine::Playlist::Repeat repeat)
{
    using Repeat = engine::Playlist::Repeat;

    QString text;

    switch (repeat) {
    case Repeat::none:
        text = "Repeat: off";
        break;

    case Repeat::all:
        text = "Repeat: all";
        break;

    case Repeat::track:
        text = "Repeat: one";
        break;
    }

    QAction* command = action(PlayerAction::repeat);
    command->setText(text);
    command->setToolTip(text);
}

void PlayerActions::set_shuffled(bool shuffled)
{
    QAction* command = action(PlayerAction::shuffle);
    if (command->isChecked() == shuffled)
        return;
    {
        const QSignalBlocker blocked{command};
        command->setChecked(shuffled);
    }
    emit command->changed();
}

} // namespace wiola::gui
