/**
 * @file
 * @brief A length of time, written the way a listener reads it.
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

#ifndef Q_MOC_RUN
#include <qt/widgets/shell/player_actions.hpp>
#include <wiola/app/playback/playback_presentation.hpp>
#endif
#include <QGroupBox>
#include <QLabel>
#include <QPointer>

#include <array>
class QPushButton;

namespace wiola::gui {

class SeekBar;
class ViewRegistry;

/// Projects playback values and emits seek intents; owns no playback or persistence policy.
class TransportView final : public QGroupBox {
    Q_OBJECT
public:
    explicit TransportView(PlayerActions& actions, QWidget* parent = nullptr);
    void register_views(ViewRegistry& registry);
    void present(const app::PlayerPresentation& state);
signals:
    void seek_requested(double fraction);

private:
    void show_time();
    PlayerActions& actions_;
    app::PlayerPresentation state_;
    QPointer<QLabel> title_, elapsed_, duration_;
    QPointer<SeekBar> seek_;
    std::array<QPointer<QPushButton>, static_cast<std::size_t>(PlayerAction::count)> buttons_;
};

} // namespace wiola::gui
