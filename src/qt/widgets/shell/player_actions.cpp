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
#include "player_shortcuts.hpp"

#include <qt/widgets/appearance/theme.hpp>

#include <QKeySequence>
#include <QSignalBlocker>
#include <QStyle>
#include <QWidget>

#include <optional>

namespace wiola::gui {

PlayerActions::PlayerActions(QWidget& styled)
    : QObject{}
{
    for (const ActionDescriptor& description : action_descriptors()) {
        auto* command = new QAction{description.label, this};

        command->setToolTip(description.label);
        command->setShortcut(QKeySequence{QString::fromLatin1(description.shortcut)});
        command->setCheckable(description.checkable);

        actions_[index_of(description.id)] = command;
    }

    new PlayerShortcuts{styled, this};
    reload_icons(styled);
    set_shuffled(false);
}

void PlayerActions::reload_icons(const QWidget& styled)
{
    for (const auto& descriptor : action_descriptors())
        if (descriptor.icon)
            action(descriptor.id)->setIcon(themed_standard_icon(styled, *descriptor.icon));

    play_icon_ = action(PlayerAction::play)->icon();
    pause_icon_ = themed_standard_icon(styled, QStyle::SP_MediaPause);
    set_playing(action(PlayerAction::play)->text() == "Pause");
}

QAction* PlayerActions::action(PlayerAction id) const noexcept
{
    return index_of(id) < actions_.size() ? actions_[index_of(id)] : nullptr;
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

void PlayerActions::set_repeat(engine::RepeatMode repeat)
{
    using Repeat = engine::RepeatMode;

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
    const QString tip =
        shuffled ? "Shuffle on — random playback order" : "Shuffle off — playlist order";
    if (command->isChecked() == shuffled && command->toolTip() == tip)
        return;
    {
        const QSignalBlocker blocked{command};
        command->setChecked(shuffled);
        command->setToolTip(tip);
    }
    emit command->changed();
}

} // namespace wiola::gui
