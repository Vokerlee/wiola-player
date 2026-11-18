/**
 * @file
 * @brief Semantic dimensions, typography, icons, and colors for player presentation.
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

#include "artwork_cache_policy.hpp"

#include <QPalette>
#include <QStyle>

#include <chrono>

namespace wiola::gui {

struct PlayerPresentationTokens {
    int window_min_width{620};
    int play_button_width{64};
    int icon_button_width{44};
    int boost_button_width{24};
    int volume_value_width{36};
    int volume_slider_width{70};
    int track_name_point_size_step{3};
    std::chrono::milliseconds refresh_interval{100};
};

struct PlaylistPresentationTokens {
    ArtworkCachePolicy artwork{};
    int min_height{120};
    int row_padding{4};
    int artist_column_width{120};
    int album_column_width{120};
    int length_column_width{60};
};

struct EqualizerPresentationTokens {
    int band_slider_height{110};
    int band_column_width{42};
    int gain_steps_per_db{10};
    int gain_step{5};
};

struct SeekPresentationTokens {
    int bar_height{12};
    int edge_width{2};
};

struct IconPresentationTokens {
    QStyle::StandardPixmap open{QStyle::SP_DirOpenIcon};
    QStyle::StandardPixmap previous{QStyle::SP_MediaSkipBackward};
    QStyle::StandardPixmap play{QStyle::SP_MediaPlay};
    QStyle::StandardPixmap pause{QStyle::SP_MediaPause};
    QStyle::StandardPixmap next{QStyle::SP_MediaSkipForward};
    QStyle::StandardPixmap stop{QStyle::SP_MediaStop};
    QStyle::StandardPixmap artwork_fallback{QStyle::SP_FileIcon};
};

struct ColorPresentationTokens {
    QPalette::ColorRole seek_track{QPalette::Mid};
    QPalette::ColorRole seek_played{QPalette::Highlight};
    QPalette::ColorRole seek_played_disabled{QPalette::Dark};
    QPalette::ColorRole seek_edge{QPalette::WindowText};
};

struct PresentationTokens {
    PlayerPresentationTokens player{};
    PlaylistPresentationTokens playlist{};
    EqualizerPresentationTokens equalizer{};
    SeekPresentationTokens seek{};
    IconPresentationTokens icons{};
    ColorPresentationTokens colors{};
};

} // namespace wiola::gui
