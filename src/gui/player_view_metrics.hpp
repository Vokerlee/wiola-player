/**
 * @file
 * @brief Presentation metrics owned by the player window.
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

#include <chrono>

namespace wiola::gui {

/// Player-window geometry and refresh policy; no engine or DSP settings live here.
struct PlayerViewMetrics {
    int window_width{620};
    int play_button_width{64};
    int icon_button_width{44};
    int boost_button_width{24};
    int volume_value_width{36};
    int volume_slider_width{70};
    int track_name_point_size_step{3};
    std::chrono::milliseconds refresh_interval{100};
};

} // namespace wiola::gui
